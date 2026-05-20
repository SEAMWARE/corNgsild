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
#include "swJsonld/swldCompactTree.h"                  // swldCompactTree, swldCompactTreeWith
#include "swJsonld/swldDownload.h"                     // swldContextFromUrl

#include "swNgsild/LdVocab.h"                          // LD_VOCAB_*
#include "swNgsild/SwNgsild.h"                         // swNgsild
#include "swNgsild/LdSubCache.h"                       // LdSubCache, LdSubCacheItem
#include "swNgsild/ldEntityToApi.h"                    // ldEntityToApi
#include "swNgsild/ldIsEntityKeyword.h"                // ldIsEntityKeyword
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
  // Entity create also triggers attributeCreated (all attrs are new) and
  // entity delete also triggers attributeDeleted (all attrs are gone).
  // The spec is silent on the symmetry — § 5.2.12 only says entityUpdated
  // = attrCreated + attrUpdated + attrDeleted — but the ETSI 046_22_12/13
  // fixtures expect the create/delete symmetry, so we follow them. When the
  // spec clarifies, we can revisit.
  //
  if (op == LdNotifyEntityCreate && (mask & LD_TRIGGER_ATTR_CREATED))
    return true;
  if (op == LdNotifyEntityDelete && (mask & LD_TRIGGER_ATTR_DELETED))
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
  // Entity delete (§ 5.8.6): {id, type, deletedAt}. sysAttrs adds
  // createdAt/modifiedAt from the pre-delete snapshot.
  //
  // When the subscription matched via attributeDeleted (because deleting
  // an entity also deletes its attributes — see triggerMatches comment),
  // additionally include each watched attribute as the extended null-marker
  // form { type, value/object/languageMap: "urn:ngsi-ld:null" }, matching
  // ETSI 046_22_12/13. Subscriptions watching all attributes (no
  // watchedAttributes filter) get every attribute the pre-delete entity
  // carried.
  //
  if (op == LdNotifyEntityDelete)
  {
    KjNode* out = kjObject(NULL, NULL);

    KjNode* srcId   = kjLookup(entityP, "id");
    KjNode* srcType = kjLookup(entityP, "type");
    if (srcId   != NULL) kjChildAdd(out, kjClone(NULL, srcId));
    if (srcType != NULL) kjChildAdd(out, kjClone(NULL, srcType));

    char     deletedIso[48];
    deletedIso[0] = 0;
    if (deletedAtNs != 0)
    {
      nsToIsoLocal(deletedAtNs, deletedIso, sizeof(deletedIso));
      kjChildAdd(out, kjString(NULL, "deletedAt", deletedIso));
    }

    if (itemP->sysAttrs)
    {
      // Storage holds createdAt/modifiedAt as KjInt (nanoseconds since
      // epoch); the entity-delete body skips the ldEntityToApi pipeline
      // that normally rewrites these to ISO 8601 strings, so do the
      // conversion inline here.
      KjNode* srcCreated  = kjLookup(entityP, LD_VOCAB_CREATED_AT);
      KjNode* srcModified = kjLookup(entityP, LD_VOCAB_MODIFIED_AT);
      if (srcCreated != NULL && srcCreated->type == KjInt)
      {
        char iso[48];
        nsToIsoLocal((uint64_t) srcCreated->value.i, iso, sizeof(iso));
        kjChildAdd(out, kjString(NULL, LD_VOCAB_CREATED_AT, iso));
      }
      else if (srcCreated != NULL)
        kjChildAdd(out, kjClone(NULL, srcCreated));
      if (srcModified != NULL && srcModified->type == KjInt)
      {
        char iso[48];
        nsToIsoLocal((uint64_t) srcModified->value.i, iso, sizeof(iso));
        kjChildAdd(out, kjString(NULL, LD_VOCAB_MODIFIED_AT, iso));
      }
      else if (srcModified != NULL)
        kjChildAdd(out, kjClone(NULL, srcModified));
    }

    int triggerMask = (itemP->triggerMask != 0) ? itemP->triggerMask : LD_TRIGGER_DEFAULT;
    // Emit per-attribute null markers (and showChanges previousX) when
    // attributeDeleted is in the trigger mask OR the subscription uses
    // showChanges (§ 5.2.14.1) — entityDeleted+showChanges still wants
    // each attribute rendered with its previous value (ETSI 046_37..39).
    if (((triggerMask & LD_TRIGGER_ATTR_DELETED) != 0) || itemP->showChanges)
    {
      for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
      {
        if (attrP->name == NULL || attrP->type != KjObject) continue;
        if (ldIsEntityKeyword(attrP->name))                 continue;

        // watchedAttributes filter (already-expanded IRIs in the cache).
        if (itemP->watchedAttrsV != NULL)
        {
          bool watched = false;
          for (int i = 0; itemP->watchedAttrsV[i] != NULL; i++)
          {
            if (strcmp(attrP->name, itemP->watchedAttrsV[i]) == 0) { watched = true; break; }
          }
          if (!watched) continue;
        }

        // Pull the type from any instance (all share the same type).
        const char* typeStr = "Property";
        KjNode* anyInstP = attrP->value.firstChildP;
        if (anyInstP != NULL && anyInstP->type == KjObject)
        {
          KjNode* tNodeP = kjLookup(anyInstP, "type");
          if (tNodeP != NULL && tNodeP->type == KjString)
            typeStr = tNodeP->value.s;
        }

        KjNode* nullAttr = kjObject(NULL, attrP->name);
        kjChildAdd(nullAttr, kjString(NULL, "type", typeStr));
        // Entity-delete fixtures (ETSI 046_37) want the bare null marker
        // even for LanguageProperty (`"languageMap": "urn:ngsi-ld:null"`),
        // matching § 5.8.6's bare form. The attribute-delete branch above
        // uses the {@none: null} form because 046_34_04 demands it — see
        // the SPEC NOTE there. testsuite-doubts.md tracks the divergence.
        //
        // Type names disambiguate on first char except 'L' (Language vs
        // List(Property|Relationship)) — typeStr[1]/[4] settle that.
        const char* primaryKey = "value";
        switch (typeStr[0])
        {
        case 'R': primaryKey = "object"; break;
        case 'V': primaryKey = "vocab";  break;
        case 'J': primaryKey = "json";   break;
        case 'L':
          if      (typeStr[1] == 'a') primaryKey = "languageMap";  // LanguageProperty
          else if (typeStr[4] == 'P') primaryKey = "valueList";    // ListProperty
          else if (typeStr[4] == 'R') primaryKey = "objectList";   // ListRelationship
          break;
        }
        kjChildAdd(nullAttr, kjString(NULL, primaryKey, LD_VOCAB_NGSILD_NULL));

        if (itemP->sysAttrs && anyInstP != NULL && anyInstP->type == KjObject)
        {
          // Per-attribute sysAttrs: pull createdAt/modifiedAt from the
          // pre-delete instance (storage form is KjInt nanoseconds — convert
          // to ISO 8601 here since the entity-delete branch bypasses
          // ldEntityToApi). Attach a fresh deletedAt mirroring the
          // entity-level one.
          KjNode* aCreated  = kjLookup(anyInstP, LD_VOCAB_CREATED_AT);
          KjNode* aModified = kjLookup(anyInstP, LD_VOCAB_MODIFIED_AT);
          if (aCreated != NULL && aCreated->type == KjInt)
          {
            char iso[48];
            nsToIsoLocal((uint64_t) aCreated->value.i, iso, sizeof(iso));
            kjChildAdd(nullAttr, kjString(NULL, LD_VOCAB_CREATED_AT, iso));
          }
          else if (aCreated != NULL)
            kjChildAdd(nullAttr, kjClone(NULL, aCreated));
          if (aModified != NULL && aModified->type == KjInt)
          {
            char iso[48];
            nsToIsoLocal((uint64_t) aModified->value.i, iso, sizeof(iso));
            kjChildAdd(nullAttr, kjString(NULL, LD_VOCAB_MODIFIED_AT, iso));
          }
          else if (aModified != NULL)
            kjChildAdd(nullAttr, kjClone(NULL, aModified));
          if (deletedIso[0] != 0)
            kjChildAdd(nullAttr, kjString(NULL, "deletedAt", deletedIso));
        }

        // showChanges (§ 5.8.6 / § 5.2.14.1): on entity-delete, every
        // attribute carries previous<X> sourced from the pre-delete
        // instance's `value`. Storage normalises typed primary keys to
        // "value", so the previousX label comes from typeStr.
        if (itemP->showChanges && anyInstP != NULL && anyInstP->type == KjObject &&
            (itemP->format == NULL || strcmp(itemP->format, "simplified") != 0))
        {
          KjNode* preVal = kjLookup(anyInstP, "value");
          if (preVal != NULL)
          {
            const char* prevKey = "previousValue";
            switch (typeStr[0])
            {
            case 'R': prevKey = "previousObject"; break;
            case 'V': prevKey = "previousVocab";  break;
            case 'J': prevKey = "previousJson";   break;
            case 'L':
              if      (typeStr[1] == 'a') prevKey = "previousLanguageMap";  // LanguageProperty
              else if (typeStr[4] == 'P') prevKey = "previousValueList";    // ListProperty
              else if (typeStr[4] == 'R') prevKey = "previousObjectList";   // ListRelationship
              break;
            }
            KjNode* prev = kjClone(NULL, preVal);
            prev->name = (char*) prevKey;
            kjChildAdd(nullAttr, prev);
          }
        }

        kjChildAdd(out, nullAttr);
      }
    }

    return out;
  }

  KjNode* entityClone = kjClone(NULL, entityP);

  //
  // Attribute-delete markers (§ 5.8.6): for every attributeDeleted change
  // in the report, inject the attribute back into the clone (the live
  // entity no longer has it).
  //
  // Shape used:
  //   "<attr>": { "@none": { "type": <T>, "value": "urn:ngsi-ld:null",
  //                          [ "deletedAt": <ns> when sysAttrs ] } }
  //
  // The `value` key is renamed to object/languageMap/... by
  // restoreValueKey based on <T>. The showChanges block below appends
  // previousValue/previousObject/previousLanguageMap onto the wrapper
  // when its trigger applies.
  //
  // SPEC NOTE — § 5.8.6 says the bare string  "<attr>": "urn:ngsi-ld:null"
  // is the minimum representation, with the object form required only when
  // sysAttrs / showChanges / datasetId triggers it. The ETSI test suite
  // (the 13 attribute-deletion fixtures under data/subscriptions/expectations
  // tagged since_v1.6.1) demands the object form unconditionally and there
  // is no upstream fix yet — so the broker emits the object form always,
  // matching the de-facto compliance bar. Flip back to a conditional default
  // once the fixtures align with v1.9.1.
  //
  // datasetId-driven multi-instance handling is not yet implemented — would
  // require emitting one wrapper per deleted dataset instance.
  //
  if (op == LdNotifyEntityUpdate && reportP != NULL && reportP->changes != NULL)
  {
    KjNode*   modAtP    = kjLookup(entityClone, LD_VOCAB_MODIFIED_AT);
    long long deletedNs = (modAtP != NULL && modAtP->type == KjInt) ? modAtP->value.i : 0;

    for (KjNode* chP = reportP->changes->value.firstChildP; chP != NULL; chP = chP->next)
    {
      KjNode* reasonP = kjLookup(chP, "reason");
      KjNode* attrP   = kjLookup(chP, "attr");
      if (reasonP == NULL || reasonP->type != KjString) continue;
      if (attrP   == NULL || attrP->type   != KjString) continue;
      if (strcmp(reasonP->value.s, "attributeDeleted") != 0) continue;

      KjNode* dsKeyP        = kjLookup(chP, "datasetId");
      const char* deletedDs = (dsKeyP != NULL && dsKeyP->type == KjString) ? dsKeyP->value.s : NULL;

      KjNode* existingAttr = kjLookup(entityClone, attrP->value.s);

      // Determine the attribute type. preValue (when present) is the
      // pre-merge wrapper; otherwise read from a surviving instance.
      const char* typeStr = "Property";
      KjNode*     preValP = kjLookup(chP, "preValue");
      if (preValP != NULL && preValP->type == KjObject && preValP->value.firstChildP != NULL)
      {
        KjNode* anyInstP = preValP->value.firstChildP;
        KjNode* tNodeP   = (anyInstP->type == KjObject) ? kjLookup(anyInstP, "type") : NULL;
        if (tNodeP != NULL && tNodeP->type == KjString)
          typeStr = tNodeP->value.s;
      }
      else if (existingAttr != NULL && existingAttr->type == KjObject && existingAttr->value.firstChildP != NULL)
      {
        KjNode* anyInstP = existingAttr->value.firstChildP;
        KjNode* tNodeP   = (anyInstP->type == KjObject) ? kjLookup(anyInstP, "type") : NULL;
        if (tNodeP != NULL && tNodeP->type == KjString)
          typeStr = tNodeP->value.s;
      }

      // Build the deleted-instance node: { type, value/lmap=null, [deletedAt] }.
      KjNode* inst = kjObject(NULL, NULL);
      kjChildAdd(inst, kjString(NULL, "type", typeStr));
      // Per § 5.8.6, LanguageProperty deletion uses `languageMap:
      // {"@none": "urn:ngsi-ld:null"}`, not the bare null marker. All
      // other types put the null marker directly as the value.
      if (strcmp(typeStr, "LanguageProperty") == 0)
      {
        KjNode* lmap = kjObject(NULL, "value");
        kjChildAdd(lmap, kjString(NULL, "@none", LD_VOCAB_NGSILD_NULL));
        kjChildAdd(inst, lmap);
      }
      else
      {
        kjChildAdd(inst, kjString(NULL, "value", LD_VOCAB_NGSILD_NULL));
      }
      if (itemP->sysAttrs == true && deletedNs != 0)
        kjChildAdd(inst, kjInteger(NULL, LD_VOCAB_DELETED_AT, deletedNs));

      if (existingAttr != NULL && deletedDs != NULL)
      {
        // Per-instance deletion (§ 5.8.6 datasetId trigger): the wrapper
        // still holds surviving instances. Add the deleted instance as
        // a fresh dsKey-keyed entry so the array form rendered downstream
        // includes it as the null-marker entry.
        inst->name = (char*) deletedDs;
        kjChildAdd(existingAttr, inst);
      }
      else if (existingAttr == NULL)
      {
        // Whole-attribute deletion: inject a new wrapper with a single
        // @none instance carrying the null marker.
        inst->name = (char*) "@none";
        KjNode* wrapper = kjObject(NULL, attrP->value.s);
        kjChildAdd(wrapper, inst);
        kjChildAdd(entityClone, wrapper);
      }
      // else: attribute still present and no datasetId — nothing to inject.
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
  if (!itemP->sysAttrs)
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
      // ({"@none":{type:..,value:..}, ...}). Storage form keeps the raw
      // value under "value" regardless of attr type — pick the right
      // previousX key based on the per-instance "type".
      KjNode* preInst = kjLookup(preValueP, "@none");
      if (preInst == NULL && preValueP->type == KjObject)
        preInst = preValueP->value.firstChildP;
      if (preInst == NULL || preInst->type != KjObject) continue;

      KjNode* preVal  = kjLookup(preInst, "value");
      KjNode* preType = kjLookup(preInst, "type");
      if (preVal == NULL) continue;

      const char* prevKey = "previousValue";
      if (preType != NULL && preType->type == KjString && preType->value.s != NULL)
      {
        const char* t = preType->value.s;
        switch (t[0])
        {
        case 'R': prevKey = "previousObject"; break;
        case 'V': prevKey = "previousVocab";  break;
        case 'J': prevKey = "previousJson";   break;
        case 'L':
          if      (t[1] == 'a') prevKey = "previousLanguageMap";  // LanguageProperty
          else if (t[4] == 'P') prevKey = "previousValueList";    // ListProperty
          else if (t[4] == 'R') prevKey = "previousObjectList";   // ListRelationship
          break;
        }
      }

      KjNode* attrOutP = kjLookup(entityClone, attrNameP->value.s);
      if (attrOutP == NULL)
      {
        // attribute was deleted from entity — add a minimal wrapper
        // carrying only the previousX marker so showChanges sees it.
        attrOutP = kjObject(NULL, attrNameP->value.s);
        kjChildAdd(entityClone, attrOutP);
        if (preType != NULL) kjChildAdd(attrOutP, kjClone(NULL, preType));
      }
      if (attrOutP->type != KjObject) continue;

      KjNode* c = kjClone(NULL, preVal);
      c->name = (char*) prevKey;
      kjChildAdd(attrOutP, c);
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

  // Format conversion is deferred to notificationSendMany — it has to run
  // AFTER the linked-entity hook attaches `entity` to Relationship
  // instances, otherwise simplified turns the Relationship into a bare
  // URI string and the inline join is silently dropped (ETSI 046_29_01).
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
    ldLinkedEntitiesHookInvoke(dataArray, itemP->notifJoin, level, itemP->sysAttrs, swNgsild.tenantP);
  }

  // Format conversion (deferred from buildNotifDataEntry so the linked-
  // entity hook runs first). simplified/keyValues read `entity` off a
  // Relationship when join=inline attached it — if simplify ran before
  // the join hook, the Relationship would already be collapsed to its
  // URI and the inlined Entity dropped on the floor.
  if (itemP->format != NULL)
  {
    void (*fmtFn)(KjNode*) = NULL;
    if (strcmp(itemP->format, "simplified") == 0 || strcmp(itemP->format, "keyValues") == 0)
      fmtFn = ldSimplifyEntity;
    else if (strcmp(itemP->format, "concise") == 0)
      fmtFn = ldConciseEntity;
    if (fmtFn != NULL)
    {
      for (KjNode* eP = dataArray->value.firstChildP; eP != NULL; eP = eP->next)
        fmtFn(eP);
    }
  }

  // Compact expanded URIs to short names (covers every data[] entry).
  // Use the sub's jsonldContext when set so user-vocabulary terms (e.g.
  // "Building", "name") compact too — without it only core-context names
  // (id/type/...) collapse and the body ships as expanded IRIs.
  SwldContext* notifCtx = NULL;
  if (itemP->contextUrl != NULL)
    notifCtx = swldContextFromUrl(itemP->contextUrl, &swRest.kalloc);
  if (notifCtx != NULL)
    swldCompactTreeWith(notification, notifCtx);
  else
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

  // ld+json notification body: per § 5.8.6 + § 6.3.5, each entity in data[]
  // (and the FeatureCollection / each Feature for geo+json — § 6.3.7) shall
  // carry @context inline. Plain application/json puts @context in the Link
  // header only; we emit that further down regardless of body shape.
  // ETSI 046_19, 046_20.
  if ((acceptLdJson || acceptGeoJson) && itemP->contextUrl != NULL)
  {
    KjNode* dataP = kjLookup(notification, "data");
    if (dataP != NULL && dataP->type == KjArray)
    {
      for (KjNode* ep = dataP->value.firstChildP; ep != NULL; ep = ep->next)
      {
        if (ep->type == KjObject && kjLookup(ep, "@context") == NULL)
          kjChildAdd(ep, kjString(NULL, "@context", itemP->contextUrl));
      }
    }
  }

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

  // § 5.8.6 / § 6.3.5 — ld+json carries @context inline; Link would be a
  // duplicate (and 046_14_01 asserts its absence). geo+json keeps Link.
  if (linkBuf[0] != 0 && !acceptLdJson)
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
