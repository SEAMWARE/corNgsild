//
// FILE            ldSubscriptionNotify.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Subscription matching and notification delivery.
//
// Matching order (fast checks first, heavy last):
//   1. status:              skip paused / expired
//   2. notificationTrigger: does this op type match?
//   3. entities[].id:       exact match
//   4. entities[].idPattern: pre-compiled regex
//   5. entities[].type:     type match
//   6. watchedAttributes:   any overlap with changed attrs?
//   7. q:                   pre-parsed q-filter evaluation
//
#include <regex.h>                                     // regexec
#include <stdio.h>                                     // snprintf
#include <string.h>                                    // strcmp, strlen, strcpy, strcat
#include <time.h>                                      // time

#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjBuilder.h"                           // kjObject, kjString, kjArray, kjChildAdd
#include "kjson/kjChildReplace.h"                      // kjChildReplace
#include "kjson/kjClone.h"                             // kjClone
#include "kjson/kjLookup.h"                            // kjLookup
#include "kjson/kjRenderSize.h"                        // kjFastRenderSize
#include "kjson/kjRender.h"                            // kjFastRender

#include "swRest/SwRestState.h"                        // swRest
#include "swRest/swRestClient.h"                       // SwRestClientRequest, etc.
#include "swJsonld/swldCompactTree.h"                  // swldCompactTree

#include "swNgsild/LdVocab.h"                          // LD_VOCAB_*
#include "swNgsild/SwNgsild.h"                         // swNgsild
#include "swNgsild/LdSubCache.h"                       // LdSubCache, LdSubCacheItem
#include "swNgsild/ldEntityToApi.h"                    // ldEntityToApi
#include "swNgsild/ldStripSysAttrs.h"                  // ldStripSysAttrs
#include "swNgsild/ldEntityMatch.h"                    // ldEntityMatchQ
#include "swNgsild/ldToGeoJson.h"                     // ldToGeoJson
#include "swNgsild/ldConformanceDowngrade.h"          // ldConformanceDowngrade
#include "swNgsild/ldEntityMerge.h"                    // LdMergeReport
#include "swJsonld/swldInit.h"                          // swldCoreContext
#include "swJsonld/SwldContext.h"                       // SwldContext
#include "swNgsild/ldSimplifyEntity.h"                 // ldSimplifyEntity, ldConciseEntity
#include "swNgsild/ldPickOmit.h"                       // ldPickOmit
#include "swNgsild/ldLangReduce.h"                     // ldLangReduce
#include "swNgsild/ldNotifyStatsHook.h"                // ldNotifyStatsHookInvoke
#include "swNgsild/ldRequestSubstitute.h"              // ldRequestSubstitute
#include "swNgsild/ldLinkedEntitiesHook.h"             // ldLinkedEntitiesHookInvoke
#include "swNgsild/ldMqttNotify.h"                     // ldIsMqttUri, ldMqttNotify
#include "swNgsild/ldSubscriptionNotify.h"             // Own interface



// -----------------------------------------------------------------------------
//
// notifIdGenerate -
//
static char* notifIdGenerate(void)
{
  static int   counter = 0;
  static char  buf[128];

  snprintf(buf, sizeof(buf), "urn:ngsi-ld:Notification:%lx:%04x", (long) time(NULL), ++counter & 0xFFFF);

  return buf;
}



// -----------------------------------------------------------------------------
//
// isoNow -
//
static void isoNow(char* buf, int bufSize)
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);

  struct tm tm;
  gmtime_r(&ts.tv_sec, &tm);

  int n = strftime(buf, bufSize, "%Y-%m-%dT%H:%M:%S", &tm);
  if (ts.tv_nsec == 0)
  {
    buf[n++] = 'Z';
    buf[n]   = 0;
  }
  else
  {
    snprintf(buf + n, bufSize - n, ".%03ldZ", ts.tv_nsec / 1000000);
  }
}



// -----------------------------------------------------------------------------
//
// triggerMatches - check if the operation matches the subscription's trigger bitmask
//
static bool triggerMatches(LdSubCacheItem* itemP, LdNotifyOp op, LdMergeReport* reportP)
{
  int mask = (itemP->triggerMask != 0) ? itemP->triggerMask : LD_TRIGGER_DEFAULT;

  //
  // Entity-level triggers
  //
  if (op == LdNotifyEntityCreate && (mask & LD_TRIGGER_ENTITY_CREATED)) return true;
  if (op == LdNotifyEntityDelete && (mask & LD_TRIGGER_ENTITY_DELETED)) return true;
  if (op == LdNotifyEntityUpdate && (mask & LD_TRIGGER_ENTITY_UPDATED)) return true;

  //
  // Entity create also triggers attributeCreated (all attrs are new)
  //
  if (op == LdNotifyEntityCreate && (mask & LD_TRIGGER_ATTR_CREATED))
    return true;

  //
  // Attribute-level triggers — check merge report reasons against the bitmask
  //
  if (op == LdNotifyEntityUpdate && reportP != NULL && reportP->changes != NULL)
  {
    int attrMask = mask & (LD_TRIGGER_ATTR_CREATED | LD_TRIGGER_ATTR_MODIFIED | LD_TRIGGER_ATTR_DELETED);

    if (attrMask != 0)
    {
      for (KjNode* chP = reportP->changes->value.firstChildP; chP != NULL; chP = chP->next)
      {
        KjNode* reasonP = kjLookup(chP, "reason");
        if (reasonP == NULL || reasonP->type != KjString)
          continue;

        if (ldTriggerFromReport(reasonP->value.s) & attrMask)
          return true;
      }
    }
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// entityHasType - check if entity has a specific type
//
static bool entityHasType(KjNode* entityTypeP, const char* type)
{
  if (entityTypeP == NULL || type == NULL)
    return false;

  if (entityTypeP->type == KjString)
    return (strcmp(entityTypeP->value.s, type) == 0);

  if (entityTypeP->type == KjArray)
  {
    for (KjNode* tP = entityTypeP->value.firstChildP; tP != NULL; tP = tP->next)
    {
      if (tP->type == KjString && strcmp(tP->value.s, type) == 0)
        return true;
    }
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// selectorMatches - check entity against a pre-parsed EntitySelector
//
static bool selectorMatches(LdSubEntitySelector* selP, const char* entityId, KjNode* entityTypeP)
{
  // Type check
  if (selP->type != NULL && !entityHasType(entityTypeP, selP->type))
    return false;

  // id check (takes precedence over idPattern)
  if (selP->id != NULL)
    return (entityId != NULL && strcmp(entityId, selP->id) == 0);

  // idPattern check (pre-compiled regex)
  if (selP->idPatternList != NULL && entityId != NULL)
  {
    for (LdSubIdPattern* ripP = selP->idPatternList; ripP != NULL; ripP = ripP->next)
    {
      if (regexec(&ripP->regex, entityId, 0, NULL, 0) == 0)
        return true;
    }
    return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// entitiesMatch - check entity against the cached entity selectors
//
static bool entitiesMatch(LdSubCacheItem* itemP, const char* entityId, KjNode* entityTypeP)
{
  if (itemP->entitySelectors == NULL)
    return true;  // no filter — matches all

  for (LdSubEntitySelector* selP = itemP->entitySelectors; selP != NULL; selP = selP->next)
  {
    if (selectorMatches(selP, entityId, entityTypeP))
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// watchedAttrsMatch - check if any changed attribute is watched
//
static bool watchedAttrsMatch(LdSubCacheItem* itemP, KjNode* entityP, LdNotifyOp op, LdMergeReport* reportP)
{
  char** watchedV = itemP->watchedAttrsV;

  if (watchedV == NULL)
    return true;  // all attributes are watched

  if (op == LdNotifyEntityCreate || op == LdNotifyEntityDelete)
  {
    for (int i = 0; watchedV[i] != NULL; i++)
    {
      if (kjLookup(entityP, watchedV[i]) != NULL)
        return true;
    }
    return false;
  }

  if (reportP != NULL && reportP->changes != NULL)
  {
    for (KjNode* chP = reportP->changes->value.firstChildP; chP != NULL; chP = chP->next)
    {
      KjNode* attrP = kjLookup(chP, "attr");
      if (attrP == NULL || attrP->type != KjString) continue;

      for (int i = 0; watchedV[i] != NULL; i++)
      {
        if (strcmp(watchedV[i], attrP->value.s) == 0)
          return true;
      }
    }
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// buildNotifDataEntry - clone + transform one entity for a notification's data[]
//
// Applies, in order: datasetId filter, ldEntityToApi, strip sys attrs,
// notification.attributes filter, notification.format. Returns a fresh
// KjNode suitable for direct insertion into the notification's data[].
//
static void nsToIsoLocal(uint64_t epochNs, char* buf, int bufSize)
{
  time_t    secs = (time_t) (epochNs / 1000000000ULL);
  long      ms   = (long) ((epochNs % 1000000000ULL) / 1000000);
  struct tm tm;

  gmtime_r(&secs, &tm);
  int n = strftime(buf, bufSize, "%Y-%m-%dT%H:%M:%S", &tm);
  if (ms > 0)
    snprintf(buf + n, bufSize - n, ".%03ldZ", ms);
  else
  {
    buf[n++] = 'Z';
    buf[n]   = 0;
  }
}


static KjNode* buildNotifDataEntry(LdSubCacheItem*       itemP,
                                   LdNotifyPendingEntry* penP)
{
  KjNode*         entityP     = penP->entityP;
  LdNotifyOp      op          = penP->op;
  LdMergeReport*  reportP     = penP->hasReport ? &penP->report : NULL;
  uint64_t        deletedAtNs = penP->deletedAtNs;

  //
  // Entity delete (§ 5.8.6): only {id, type, deletedAt}. sysAttrs
  // adds createdAt/modifiedAt from the pre-delete snapshot.
  //
  if (op == LdNotifyEntityDelete)
  {
    KjNode* out = kjObject(NULL, NULL);

    KjNode* srcId   = kjLookup(entityP, "id");
    KjNode* srcType = kjLookup(entityP, "type");
    if (srcId   != NULL) kjChildAdd(out, kjClone(NULL, srcId));
    if (srcType != NULL) kjChildAdd(out, kjClone(NULL, srcType));

    if (deletedAtNs != 0)
    {
      char iso[48];
      nsToIsoLocal(deletedAtNs, iso, sizeof(iso));
      kjChildAdd(out, kjString(NULL, "deletedAt", iso));
    }

    if (itemP->sysAttrs)
    {
      KjNode* srcCreated  = kjLookup(entityP, LD_VOCAB_CREATED_AT);
      KjNode* srcModified = kjLookup(entityP, LD_VOCAB_MODIFIED_AT);
      if (srcCreated  != NULL) kjChildAdd(out, kjClone(NULL, srcCreated));
      if (srcModified != NULL) kjChildAdd(out, kjClone(NULL, srcModified));
    }

    return out;
  }

  KjNode* entityClone = kjClone(NULL, entityP);

  //
  // Attribute-delete markers (§ 5.8.6): for every attributeDeleted
  // change in the report, inject "<attr>": "urn:ngsi-ld:null" into
  // the clone (the live entity no longer has the attribute).
  //
  if (op == LdNotifyEntityUpdate && reportP != NULL && reportP->changes != NULL)
  {
    for (KjNode* chP = reportP->changes->value.firstChildP; chP != NULL; chP = chP->next)
    {
      KjNode* reasonP = kjLookup(chP, "reason");
      KjNode* attrP   = kjLookup(chP, "attr");
      if (reasonP == NULL || reasonP->type != KjString) continue;
      if (attrP   == NULL || attrP->type   != KjString) continue;
      if (strcmp(reasonP->value.s, "attributeDeleted") != 0) continue;
      if (kjLookup(entityClone, attrP->value.s) != NULL)    continue;

      kjChildAdd(entityClone, kjString(NULL, attrP->value.s, "urn:ngsi-ld:null"));
    }
  }

  //
  // datasetId filter (§ 5.8.6): if the subscription specifies a datasetId
  // list, keep only matching instances within each attribute wrapper.
  // Must run BEFORE ldEntityToApi (which unwraps the dataset-keyed storage
  // format). In storage, each attr is { "@none": {...}, "urn:ds:1": {...} }.
  //
  if (itemP->datasetIdV != NULL)
  {
    KjNode* attrP = entityClone->value.firstChildP;
    while (attrP != NULL)
    {
      KjNode* nextAttr = attrP->next;

      if (attrP->type != KjObject || attrP->name == NULL ||
          strcmp(attrP->name, "id") == 0 || strcmp(attrP->name, "type") == 0)
      {
        attrP = nextAttr;
        continue;
      }

      KjNode* instP = attrP->value.firstChildP;
      while (instP != NULL)
      {
        KjNode* nextInst = instP->next;
        bool keep = false;

        for (int i = 0; itemP->datasetIdV[i] != NULL; i++)
        {
          if (strcmp(instP->name, itemP->datasetIdV[i]) == 0)
          {
            keep = true;
            break;
          }
        }

        if (!keep)
          kjChildRemove(attrP, instP);

        instP = nextInst;
      }

      if (attrP->value.firstChildP == NULL)
        kjChildRemove(entityClone, attrP);

      attrP = nextAttr;
    }
  }

  ldEntityToApi(entityClone, &swRest.kalloc);
  ldStripSysAttrs(entityClone);

  //
  // showChanges (§ 5.8.6 / § 5.2.14.1): for each report change that
  // carries a preValue, add previousValue / previousObject /
  // previousLanguageMap inside the corresponding attr wrapper in
  // entityClone. showChanges is forbidden with keyValues/simplified
  // per spec, so we only emit it when format is default/concise.
  //
  if (itemP->showChanges && reportP != NULL && reportP->changes != NULL &&
      (itemP->format == NULL || strcmp(itemP->format, "simplified") != 0))
  {
    for (KjNode* chP = reportP->changes->value.firstChildP; chP != NULL; chP = chP->next)
    {
      KjNode* attrNameP = kjLookup(chP, "attr");
      KjNode* preValueP = kjLookup(chP, "preValue");
      if (attrNameP == NULL || attrNameP->type != KjString) continue;
      if (preValueP == NULL) continue;

      // preValue is the pre-change dataset-keyed wrapper
      // ({"@none":{type:..,value:..}, ...}). Pull the @none instance's
      // current/object/languageMap.
      KjNode* preInst = kjLookup(preValueP, "@none");
      if (preInst == NULL && preValueP->type == KjObject)
        preInst = preValueP->value.firstChildP;
      if (preInst == NULL || preInst->type != KjObject) continue;

      KjNode* preVal   = kjLookup(preInst, "value");
      KjNode* preObj   = kjLookup(preInst, "object");
      KjNode* preLmap  = kjLookup(preInst, "languageMap");

      KjNode* attrOutP = kjLookup(entityClone, attrNameP->value.s);
      if (attrOutP == NULL)
      {
        // attribute was deleted from entity — add a minimal wrapper
        // carrying only the previousX marker so showChanges sees it.
        attrOutP = kjObject(NULL, attrNameP->value.s);
        kjChildRemove(entityClone, kjLookup(entityClone, attrNameP->value.s));
        kjChildAdd(entityClone, attrOutP);
        // Set type from the preInst for correctness
        KjNode* preType = kjLookup(preInst, "type");
        if (preType != NULL) kjChildAdd(attrOutP, kjClone(NULL, preType));
      }
      if (attrOutP->type != KjObject) continue;

      if (preVal  != NULL) { KjNode* c = kjClone(NULL, preVal);  c->name = (char*) "previousValue";       kjChildAdd(attrOutP, c); }
      if (preObj  != NULL) { KjNode* c = kjClone(NULL, preObj);  c->name = (char*) "previousObject";      kjChildAdd(attrOutP, c); }
      if (preLmap != NULL) { KjNode* c = kjClone(NULL, preLmap); c->name = (char*) "previousLanguageMap"; kjChildAdd(attrOutP, c); }
    }
  }

  if (itemP->notifAttrsV != NULL)
  {
    KjNode* childP = entityClone->value.firstChildP;
    while (childP != NULL)
    {
      KjNode* nextP = childP->next;

      if (childP->name != NULL &&
          strcmp(childP->name, "id")    != 0 &&
          strcmp(childP->name, "type")  != 0 &&
          strcmp(childP->name, "scope") != 0)
      {
        bool keep = false;
        for (int i = 0; itemP->notifAttrsV[i] != NULL; i++)
        {
          if (strcmp(childP->name, itemP->notifAttrsV[i]) == 0)
          {
            keep = true;
            break;
          }
        }
        if (!keep)
          kjChildRemove(entityClone, childP);
      }

      childP = nextP;
    }
  }

  // notification.pick / notification.omit (§ 5.2.14, § 4.21) — entity-member
  // projection on the notification body. Applied before format conversion
  // so the chosen format operates on the already-reduced entity.
  if (itemP->notifPickV != NULL || itemP->notifOmitV != NULL)
    ldPickOmit(entityClone, itemP->notifPickV, itemP->notifOmitV);

  // Subscription-level lang (§ 4.15) — collapse LanguageMap attrs to the
  // selected language before format conversion.
  if (itemP->lang != NULL && itemP->lang[0] != 0)
    ldLangReduce(entityClone, itemP->lang, &swRest.kalloc);

  if (itemP->format != NULL)
  {
    if (strcmp(itemP->format, "simplified") == 0 || strcmp(itemP->format, "keyValues") == 0)
      ldSimplifyEntity(entityClone);
    else if (strcmp(itemP->format, "concise") == 0)
      ldConciseEntity(entityClone);
  }

  return entityClone;
}



// -----------------------------------------------------------------------------
//
// notificationSendMany - build a Notification payload with N entities in data[]
// and POST it to the subscription's endpoint.
//
static void notificationSendMany(LdSubCacheItem* itemP, LdNotifyPendingEntry** entries, int n)
{
  if (itemP->endpointUri == NULL || itemP->subId == NULL || n == 0)
    return;

  // § 5.2.15 endpoint.cooldown — skip if inside the cooldown window after
  // the last failure. Default 30s when notification.endpoint.cooldown is
  // unspecified.
  if (itemP->lastFailure > 0)
  {
    uint64_t cool = (itemP->cooldownNs != 0) ? itemP->cooldownNs : 30000000000ULL;
    if (itemP->lastFailure + cool > swRest.requestStartTime)
      return;
  }

  char isoTimeBuf[64];
  isoNow(isoTimeBuf, sizeof(isoTimeBuf));

  KjNode* notification = kjObject(NULL, NULL);
  kjChildAdd(notification, kjString(NULL, "id",             notifIdGenerate()));
  kjChildAdd(notification, kjString(NULL, "type",           "Notification"));
  kjChildAdd(notification, kjString(NULL, "subscriptionId", itemP->subId));
  kjChildAdd(notification, kjString(NULL, "notifiedAt",     isoTimeBuf));

  KjNode* dataArray = kjArray(NULL, "data");
  for (int i = 0; i < n; i++)
    kjChildAdd(dataArray, buildNotifDataEntry(itemP, entries[i]));
  kjChildAdd(notification, dataArray);

  // § 5.2.14 notification.join — linked-entity retrieval (§ 4.5.23).
  // Hook is installed by the broker (it owns db.* and the reg cache);
  // a no-op when the broker hasn't registered or join is "@none".
  if (itemP->notifJoin != NULL && strcmp(itemP->notifJoin, "@none") != 0)
  {
    int level = (itemP->notifJoinLevel > 0) ? itemP->notifJoinLevel : 1;
    ldLinkedEntitiesHookInvoke(dataArray, itemP->notifJoin, level, swNgsild.tenantP);
  }

  // Compact expanded URIs to short names (covers every data[] entry)
  swldCompactTree(notification);

  // § 5.2.12 / § 4.3.6.8: backwards-compat downgrade of entity payloads
  // when the subscription requested an older NGSI-LD version.
  if (itemP->conformanceMajor != 0 || itemP->conformanceMinor != 0)
  {
    KjNode* dataP = kjLookup(notification, "data");
    if (dataP != NULL)
      ldConformanceDowngrade(dataP, itemP->conformanceMajor, itemP->conformanceMinor, swRest.kjsonP);
  }

  // § 5.2.14: when endpoint.accept is application/geo+json, replace the
  // `data` array with a FeatureCollection. Each entity becomes a Feature
  // with id at the top level, geometry from the GeoProperty, and the rest
  // of the entity as `properties`.
  bool acceptGeoJson = (itemP->endpointAccept != NULL &&
                        strcmp(itemP->endpointAccept, "application/geo+json") == 0);
  bool acceptLdJson  = (itemP->endpointAccept != NULL &&
                        strcmp(itemP->endpointAccept, "application/ld+json") == 0);

  if (acceptGeoJson)
  {
    KjNode* oldDataP = kjLookup(notification, "data");
    if (oldDataP != NULL && oldDataP->type == KjArray)
    {
      KjNode* newDataP = oldDataP;
      ldToGeoJson(&newDataP, NULL /* default "location" */, swRest.kjsonP);
      if (newDataP != NULL && newDataP != oldDataP)
      {
        newDataP->name = (char*) "data";
        kjChildReplace(notification, oldDataP, newDataP);
      }
    }
  }

  //
  // Render to JSON
  //
  int   bodySize = kjFastRenderSize(notification) + 1;
  char* body     = (char*) kaAlloc(&swRest.kalloc, bodySize);

  kjFastRender(notification, body);

  //
  // Compute Link header — needed for both HTTP and MQTT paths.
  //
  const char* ctxUrl = itemP->contextUrl;
  if (ctxUrl == NULL)
  {
    SwldContext* coreP = swldCoreContext();
    if (coreP != NULL)
      ctxUrl = coreP->url;
  }

  char linkBuf[512];
  linkBuf[0] = 0;
  if (ctxUrl != NULL)
  {
    snprintf(linkBuf, sizeof(linkBuf),
             "<%s>; rel=\"http://www.w3.org/ns/json-ld#context\"; type=\"application/ld+json\"",
             ctxUrl);
  }

  //
  // Content-Type negotiation (§ 5.2.14 endpoint.accept):
  //   application/ld+json  → JSON-LD (Link header omitted; @context inline if any)
  //   application/geo+json → GeoJSON FeatureCollection (already converted above)
  //   default              → application/json (Link header carries @context)
  //
  const char* contentType = "application/json";
  if (acceptGeoJson)      contentType = "application/geo+json";
  else if (acceptLdJson)  contentType = "application/ld+json";

  //
  // MQTT delivery path (§ 7) — when endpoint.uri is mqtt[s]://...
  //
  if (ldIsMqttUri(itemP->endpointUri))
  {
    bool ok = ldMqttNotify(itemP->endpointUri, body,
                           contentType,
                           (linkBuf[0] != 0) ? linkBuf : NULL,
                           itemP->receiverInfo,
                           itemP->notifierInfo);

    itemP->timesSent       += 1;
    itemP->lastNotification = swRest.requestStartTime;
    if (ok)
      itemP->lastSuccess = swRest.requestStartTime;
    else
    {
      itemP->timesFailed += 1;
      itemP->lastFailure  = swRest.requestStartTime;
    }
    ldNotifyStatsHookInvoke(false /*csrSub*/, ok);
    return;
  }

  //
  // Send HTTP POST
  //
  SwRestClientRequest  req;
  SwRestClientResponse resp;

  swRestClientRequestInit(&req, SwVerbPost, itemP->endpointUri, NULL);
  swRestClientRequestHeader(&req, "Content-Type", contentType);

  if (linkBuf[0] != 0)
    swRestClientRequestHeader(&req, "Link", linkBuf);

  // § 5.2.15 endpoint.receiverInfo — emit each {key,value} as a request header
  if (itemP->receiverInfo != NULL && itemP->receiverInfo->type == KjArray)
  {
    for (KjNode* kvP = itemP->receiverInfo->value.firstChildP; kvP != NULL; kvP = kvP->next)
    {
      if (kvP->type != KjObject) continue;
      KjNode* kP = kjLookup(kvP, "key");
      KjNode* vP = kjLookup(kvP, "value");
      if (kP != NULL && kP->type == KjString && vP != NULL && vP->type == KjString)
      {
        const char* hv = ldRequestSubstitute(kP->value.s, vP->value.s);
        if (hv != NULL)
          swRestClientRequestHeader(&req, kP->value.s, hv);
      }
    }
  }

  swRestClientRequestBody(&req, body, strlen(body));
  // § 5.2.15 endpoint.timeout — per-sub override; default 10s
  int reqTmoMs = (itemP->timeoutMs > 0) ? itemP->timeoutMs : 10000;
  swRestClientRequestTimeout(&req, 5000, reqTmoMs);

  swRestClientSend(&req, &resp);

  //
  // Update notification counters (use request timestamp, nanoseconds)
  //
  itemP->timesSent       += 1;
  itemP->lastNotification = swRest.requestStartTime;

  bool ok = (resp.statusCode >= 200 && resp.statusCode < 300);
  if (ok)
    itemP->lastSuccess = swRest.requestStartTime;
  else
  {
    itemP->timesFailed += 1;
    itemP->lastFailure  = swRest.requestStartTime;
  }

  ldNotifyStatsHookInvoke(false /*csrSub*/, ok);
}



// -----------------------------------------------------------------------------
//
// ldSubscriptionNotifyBatch -
//
void ldSubscriptionNotifyBatch(LdSubCache*           cacheP,
                               LdNotifyPendingEntry* pendingV,
                               int                   pendingN)
{
  if (cacheP == NULL || pendingV == NULL || pendingN == 0)
    return;

  //
  // Outer: per subscription. Inner: per pending entry. Collect per-sub
  // matches in encounter-order, then emit ONE notification per sub whose
  // data[] carries each matched entity (one entry per merged instance).
  //
  for (LdSubCacheItem* itemP = cacheP->itemList; itemP != NULL; itemP = itemP->next)
  {
    //
    // Per-sub static checks (done once, regardless of pending count)
    //
    if (itemP->status != NULL)
    {
      if (strcmp(itemP->status, "paused")  == 0) continue;
      if (strcmp(itemP->status, "expired") == 0) continue;
    }

    if (itemP->expiresAt > 0 && swRest.requestStartTime > itemP->expiresAt)
    {
      itemP->status = "expired";
      continue;
    }

    if (itemP->throttling > 0 && itemP->lastNotification > 0)
    {
      uint64_t throttlingNs = (uint64_t) (itemP->throttling * 1e9);
      if ((swRest.requestStartTime - itemP->lastNotification) < throttlingNs)
        continue;
    }

    //
    // Per-pending match pass
    //
    LdNotifyPendingEntry** matched  = (LdNotifyPendingEntry**) kaAlloc(&swRest.kalloc, pendingN * sizeof(LdNotifyPendingEntry*));
    int                    matchedN = 0;

    for (int i = 0; i < pendingN; i++)
    {
      LdNotifyPendingEntry* p = &pendingV[i];
      if (p->entityP == NULL)
        continue;

      LdMergeReport* reportP = p->hasReport ? &p->report : NULL;

      KjNode*     entityIdP   = kjLookup(p->entityP, "id");
      KjNode*     entityTypeP = kjLookup(p->entityP, "type");
      const char* entityId    = (entityIdP != NULL && entityIdP->type == KjString) ? entityIdP->value.s : NULL;

      if (!triggerMatches(itemP, p->op, reportP))
        continue;

      if (!entitiesMatch(itemP, entityId, entityTypeP))
        continue;

      if (!watchedAttrsMatch(itemP, p->entityP, p->op, reportP))
        continue;

      if (itemP->scopeExpr != NULL)
      {
        KjNode* scopeP = kjLookup(p->entityP, LD_VOCAB_SCOPE);
        if (!ldEntityMatchScope(scopeP, itemP->scopeExpr))
          continue;
      }

      if (itemP->qExpr != NULL)
      {
        if (!ldEntityMatchQ(p->entityP, itemP->qExpr))
          continue;
      }

      if (itemP->geoRel != NULL && cacheP->geoMatchFunc != NULL)
      {
        if (!cacheP->geoMatchFunc(p->entityP, itemP->geoRel, itemP->geoGeometry,
                                  itemP->geoCoordinates, itemP->geoProperty))
          continue;
      }

      matched[matchedN++] = p;
    }

    if (matchedN > 0)
      notificationSendMany(itemP, matched, matchedN);
  }
}
