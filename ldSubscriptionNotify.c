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
#include "kjson/kjClone.h"                             // kjClone
#include "kjson/kjLookup.h"                            // kjLookup
#include "kjson/kjRenderSize.h"                        // kjFastRenderSize
#include "kjson/kjRender.h"                            // kjFastRender

#include "swRest/SwRestState.h"                        // swRest
#include "swRest/swRestClient.h"                       // SwRestClientRequest, etc.
#include "swJsonld/swldCompactTree.h"                  // swldCompactTree

#include "swNgsild/LdVocab.h"                          // LD_VOCAB_*
#include "swNgsild/LdSubCache.h"                       // LdSubCache, LdSubCacheItem
#include "swNgsild/ldEntityToApi.h"                    // ldEntityToApi
#include "swNgsild/ldStripSysAttrs.h"                  // ldStripSysAttrs
#include "swNgsild/ldEntityMatch.h"                    // ldEntityMatchQ
#include "swNgsild/ldEntityMerge.h"                    // LdMergeReport
#include "swJsonld/swldInit.h"                          // swldCoreContext
#include "swJsonld/SwldContext.h"                       // SwldContext
#include "swNgsild/ldSimplifyEntity.h"                 // ldSimplifyEntity, ldConciseEntity
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
// notificationSend - build and POST the notification payload
//
static void notificationSend(LdSubCacheItem* itemP, KjNode* entityP)
{
  if (itemP->endpointUri == NULL || itemP->subId == NULL)
    return;

  //
  // Build the Notification payload (per spec 5.3.1)
  //
  char isoTimeBuf[64];
  isoNow(isoTimeBuf, sizeof(isoTimeBuf));

  KjNode* notification = kjObject(NULL, NULL);

  kjChildAdd(notification, kjString(NULL, "id",             notifIdGenerate()));
  kjChildAdd(notification, kjString(NULL, "type",           "Notification"));
  kjChildAdd(notification, kjString(NULL, "subscriptionId", itemP->subId));
  kjChildAdd(notification, kjString(NULL, "notifiedAt",     isoTimeBuf));

  // data: array of entities — convert from DB storage format to API format
  KjNode* dataArray    = kjArray(NULL, "data");
  KjNode* entityClone  = kjClone(NULL, entityP);

  //
  // datasetId filter (§ 5.8.6): if the subscription specifies a datasetId
  // list, keep only matching instances within each attribute wrapper.
  // Must run BEFORE ldEntityToApi (which unwraps the dataset-keyed storage
  // format). In storage, each attr is { "@none": {...}, "urn:ds:1": {...} }.
  // The filter removes instances whose dsKey is not in the list.
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

      // Walk instances within the attr wrapper
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

      // If all instances were removed, remove the attr wrapper too
      if (attrP->value.firstChildP == NULL)
        kjChildRemove(entityClone, attrP);

      attrP = nextAttr;
    }
  }

  ldEntityToApi(entityClone, &swRest.kalloc);
  ldStripSysAttrs(entityClone);

  //
  // Filter attributes if notification.attributes is specified
  //
  if (itemP->notifAttrsV != NULL)
  {
    KjNode* childP = entityClone->value.firstChildP;
    while (childP != NULL)
    {
      KjNode* nextP = childP->next;

      // Keep id, type, scope — filter everything else
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

  kjChildAdd(dataArray, entityClone);
  kjChildAdd(notification, dataArray);

  // Compact expanded URIs to short names
  swldCompactTree(notification);

  //
  // Apply notification format (simplified / concise / normalized)
  //
  if (itemP->format != NULL)
  {
    if (strcmp(itemP->format, "simplified") == 0 || strcmp(itemP->format, "keyValues") == 0)
      ldSimplifyEntity(entityClone);
    else if (strcmp(itemP->format, "concise") == 0)
      ldConciseEntity(entityClone);
  }

  //
  // Render to JSON
  //
  int   bodySize = kjFastRenderSize(notification) + 1;
  char* body     = (char*) kaAlloc(&swRest.kalloc, bodySize);

  kjFastRender(notification, body);

  //
  // Send HTTP POST
  //
  SwRestClientRequest  req;
  SwRestClientResponse resp;

  swRestClientRequestInit(&req, SwVerbPost, itemP->endpointUri, NULL);
  swRestClientRequestHeader(&req, "Content-Type", "application/json");

  //
  // Link header with the @context URL for the notification
  //
  const char* ctxUrl = itemP->contextUrl;
  if (ctxUrl == NULL)
  {
    SwldContext* coreP = swldCoreContext();
    if (coreP != NULL)
      ctxUrl = coreP->url;
  }

  if (ctxUrl != NULL)
  {
    char linkBuf[512];
    snprintf(linkBuf, sizeof(linkBuf),
             "<%s>; rel=\"http://www.w3.org/ns/json-ld#context\"; type=\"application/ld+json\"",
             ctxUrl);
    swRestClientRequestHeader(&req, "Link", linkBuf);
  }

  swRestClientRequestBody(&req, body, strlen(body));
  swRestClientRequestTimeout(&req, 5000, 10000);

  swRestClientSend(&req, &resp);

  //
  // Update notification counters (use request timestamp, nanoseconds)
  //
  itemP->timesSent       += 1;
  itemP->lastNotification = swRest.requestStartTime;

  if (resp.statusCode >= 200 && resp.statusCode < 300)
    itemP->lastSuccess = swRest.requestStartTime;
  else
  {
    itemP->timesFailed += 1;
    itemP->lastFailure  = swRest.requestStartTime;
  }
}



// -----------------------------------------------------------------------------
//
// ldSubscriptionNotify -
//
void ldSubscriptionNotify(LdSubCache*     cacheP,
                          KjNode*         entityP,
                          LdNotifyOp      op,
                          LdMergeReport*  reportP)
{
  if (cacheP == NULL || entityP == NULL)
    return;

  //
  // Extract entity id and type for matching
  //
  KjNode*     entityIdP   = kjLookup(entityP, "id");
  KjNode*     entityTypeP = kjLookup(entityP, "type");
  const char* entityId    = (entityIdP != NULL && entityIdP->type == KjString) ? entityIdP->value.s : NULL;

  //
  // Walk cached subscriptions
  //
  for (LdSubCacheItem* itemP = cacheP->itemList; itemP != NULL; itemP = itemP->next)
  {
    //
    // 1. Check status — skip paused / expired
    //
    if (itemP->status != NULL)
    {
      if (strcmp(itemP->status, "paused")  == 0) continue;
      if (strcmp(itemP->status, "expired") == 0) continue;
    }

    //
    // 1b. Dynamic expiration check (using request timestamp, nanoseconds)
    //
    if (itemP->expiresAt > 0 && swRest.requestStartTime > itemP->expiresAt)
    {
      itemP->status = "expired";
      continue;
    }

    //
    // 2. Check notificationTrigger
    //
    if (!triggerMatches(itemP, op, reportP))
      continue;

    //
    // 3-5. Check entities[] (id, idPattern, type) — pre-parsed selectors
    //
    if (!entitiesMatch(itemP, entityId, entityTypeP))
      continue;

    //
    // 6. Check watchedAttributes — pre-parsed array
    //
    if (!watchedAttrsMatch(itemP, entityP, op, reportP))
      continue;

    //
    // 7. scopeQ — pre-parsed scope expression
    //
    if (itemP->scopeExpr != NULL)
    {
      KjNode* scopeP = kjLookup(entityP, LD_VOCAB_SCOPE);
      if (!ldEntityMatchScope(scopeP, itemP->scopeExpr))
        continue;
    }

    //
    // 8. q-filter — pre-parsed LdQNode tree
    //
    if (itemP->qExpr != NULL)
    {
      if (!ldEntityMatchQ(entityP, itemP->qExpr))
        continue;
    }

    //
    // 8. geoQ — use registered callback (GEOS lives in the DB plugin, not here)
    //
    if (itemP->geoRel != NULL && cacheP->geoMatchFunc != NULL)
    {
      if (!cacheP->geoMatchFunc(entityP, itemP))
        continue;
    }

    //
    // 9. Throttling — skip if last notification was too recent
    //
    if (itemP->throttling > 0 && itemP->lastNotification > 0)
    {
      uint64_t throttlingNs = (uint64_t) (itemP->throttling * 1e9);
      if ((swRest.requestStartTime - itemP->lastNotification) < throttlingNs)
        continue;
    }

    //
    // Match! Send notification.
    //
    notificationSend(itemP, entityP);
  }
}
