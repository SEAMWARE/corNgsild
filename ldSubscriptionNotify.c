//
// FILE            ldSubscriptionNotify.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
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

#include "corAlloc/corAlloc.h"                         // corAlloc
#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corLog/corLog.h"                             // COR_T
#include "corTree/CorNode.h"                           // CorNode
#include "corTree/corTreeBuilder.h"                    // corTreeObject, corTreeString, corTreeArray, corTreeChildAdd
#include "corTree/corTreeChildReplace.h"               // corTreeChildReplace
#include "corTree/corTreeClone.h"                      // corTreeClone
#include "corTree/corTreeLookup.h"                     // corTreeLookup
#include "corJson/corJsonRenderSize.h"                 // corJsonFastRenderSize
#include "corJson/corJsonRender.h"                     // corJsonFastRender

#include "corRest/CorRestState.h"                        // corRest
#include "corRest/corRestClient.h"                       // CorRestClientRequest, etc.
#include "corJsonld/corLdCompactTree.h"                  // corLdCompactTree, corLdCompactTreeWith
#include "corJsonld/corLdDownload.h"                     // corLdContextFromUrl

#include "corNgsild/ldInstanceWritten.h"                // ldInstanceWritten
#include "corNgsild/ldTraceLevels.h"                    // LdTNotif*
#include "corNgsild/ldTypes.h"                          // ldFormatToString
#include "corNgsild/LdVocab.h"                          // LD_VOCAB_*
#include "corNgsild/CorNgsild.h"                         // corNgsild
#include "corNgsild/LdSubCache.h"                       // LdSubCache, LdSubCacheItem
#include "corNgsild/ldSubCache.h"                       // ldSubCacheRdLock, ldSubCacheItemPin, ...
#include "corNgsild/ldEntityToApi.h"                    // ldEntityToApi
#include "corNgsild/ldIsEntityKeyword.h"                // ldIsEntityKeyword
#include "corNgsild/ldStripSysAttrs.h"                  // ldStripSysAttrs
#include "corNgsild/ldEntityMatch.h"                    // ldEntityMatchQ
#include "corNgsild/ldToGeoJson.h"                     // ldToGeoJson
#include "corNgsild/ldConformanceDowngrade.h"          // ldConformanceDowngrade
#include "corNgsild/ldEntityMerge.h"                    // LdMergeReport
#include "corJsonld/corLdInit.h"                          // corLdCoreContext
#include "corJsonld/CorLdContext.h"                       // CorLdContext
#include "corNgsild/ldRender.h"                          // ldToConcise, ldToSimplified
#include "corNgsild/ldPickOmit.h"                       // ldPickOmit
#include "corNgsild/ldLangReduce.h"                     // ldLangReduce
#include "corNgsild/ldNotifyStatsHook.h"                // ldNotifyStatsHookInvoke
#include "corNgsild/ldRequestSubstitute.h"              // ldRequestSubstitute
#include "corNgsild/ldLinkedEntitiesHook.h"             // ldLinkedEntitiesHookInvoke
#include "corNgsild/ldMqttNotify.h"                     // ldIsMqttUri, ldMqttNotify
#include "corNgsild/ldThrottleDirty.h"                  // ldThrottleDirtyUpsert/Drain/EntriesFree
#include "corNgsild/ldPeriodicLoop.h"                   // ldPeriodicLoopRegister
#include "corNgsild/ldSubscriptionNotify.h"             // Own interface



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
static bool triggerMatches(LdSubCacheItem* itemP, LdNotifyOp op, int reasonsMask)
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
  // Attribute-level triggers — the per-entry reasonsMask was computed once
  // at defer time; the per-sub check is a single AND.
  //
  if (op == LdNotifyEntityUpdate &&
      (mask & reasonsMask & (LD_TRIGGER_ATTR_CREATED | LD_TRIGGER_ATTR_MODIFIED | LD_TRIGGER_ATTR_DELETED)) != 0)
    return true;

  return false;
}



// -----------------------------------------------------------------------------
//
// selectorMatches - check entity against a pre-parsed EntitySelector
//
static bool selectorMatches(LdSubEntitySelector* selP, const char* entityId, CorNode* entityTypeP)
{
  // Type check — use the parsed § 4.17 expression so (A|B), A&B and
  // !A operators match correctly. typeExpr is set whenever type is.
  if (selP->typeExpr != NULL && !ldEntityMatchType(entityTypeP, selP->typeExpr))
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
static bool entitiesMatch(LdSubCacheItem* itemP, const char* entityId, CorNode* entityTypeP)
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
// An entry of watchedDsV names ONE instance ("attr@datasetId"): the change must
// then have written that instance, not just the Attribute.
//
static bool watchedAttrsMatch(LdSubCacheItem* itemP, CorNode* entityP, LdNotifyOp op, LdMergeReport* reportP)
{
  char** watchedV = itemP->watchedAttrsV;
  char** dsV      = itemP->watchedDsV;

  if (watchedV == NULL)
    return true;  // all attributes are watched

  if (op == LdNotifyEntityCreate || op == LdNotifyEntityDelete)
  {
    for (int i = 0; watchedV[i] != NULL; i++)
    {
      CorNode* attrP = corTreeLookup(entityP, watchedV[i]);

      if (attrP == NULL)
        continue;

      if ((dsV == NULL) || (dsV[i] == NULL) || (corTreeLookup(attrP, dsV[i]) != NULL))
        return true;
    }
    return false;
  }

  if (reportP != NULL && reportP->changes != NULL)
  {
    for (CorNode* chP = reportP->changes->value.head; chP != NULL; chP = chP->next)
    {
      CorNode* attrP = corTreeLookup(chP, "attr");
      if (attrP == NULL || attrP->type != CorString) continue;

      for (int i = 0; watchedV[i] != NULL; i++)
      {
        if (strcmp(watchedV[i], attrP->value.s) != 0)
          continue;

        if ((dsV == NULL) || (dsV[i] == NULL))
          return true;

        //
        // The post-change Attribute is the entity's own - absent when the change
        // removed it entirely. The reason cannot tell: deleting ONE instance is an
        // attributeDeleted too, with the Attribute's other instances still there.
        //
        if (ldInstanceWritten(corTreeLookup(chP, "preValue"), corTreeLookup(entityP, watchedV[i]), dsV[i]))
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
// CorNode suitable for direct insertion into the notification's data[].
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


static CorNode* buildNotifDataEntry(LdSubCacheItem*      itemP,
                                   LdNotifyPendingEntry* penP)
{
  CorNode*        entityP     = penP->entityP;
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
    CorNode* out = corTreeObject(corRest.kallocP, NULL);

    CorNode* srcId  = corTreeLookup(entityP, "id");
    CorNode* srcType = corTreeLookup(entityP, "type");
    if (srcId   != NULL) corTreeChildAdd(out, corTreeClone(corRest.kallocP, srcId));
    if (srcType != NULL) corTreeChildAdd(out, corTreeClone(corRest.kallocP, srcType));

    char     deletedIso[48];
    deletedIso[0] = 0;
    if (deletedAtNs != 0)
    {
      nsToIsoLocal(deletedAtNs, deletedIso, sizeof(deletedIso));
      corTreeChildAdd(out, corTreeString(corRest.kallocP, "deletedAt", deletedIso));
    }

    if (itemP->sysAttrs)
    {
      // Storage holds createdAt/modifiedAt as CorInt (nanoseconds since
      // epoch); the entity-delete body skips the ldEntityToApi pipeline
      // that normally rewrites these to ISO 8601 strings, so do the
      // conversion inline here.
      CorNode* srcCreated = corTreeLookup(entityP, LD_VOCAB_CREATED_AT);
      CorNode* srcModified = corTreeLookup(entityP, LD_VOCAB_MODIFIED_AT);
      if (srcCreated != NULL && srcCreated->type == CorInt)
      {
        char iso[48];
        nsToIsoLocal((uint64_t) srcCreated->value.i, iso, sizeof(iso));
        corTreeChildAdd(out, corTreeString(corRest.kallocP, LD_VOCAB_CREATED_AT, iso));
      }
      else if (srcCreated != NULL)
        corTreeChildAdd(out, corTreeClone(corRest.kallocP, srcCreated));
      if (srcModified != NULL && srcModified->type == CorInt)
      {
        char iso[48];
        nsToIsoLocal((uint64_t) srcModified->value.i, iso, sizeof(iso));
        corTreeChildAdd(out, corTreeString(corRest.kallocP, LD_VOCAB_MODIFIED_AT, iso));
      }
      else if (srcModified != NULL)
        corTreeChildAdd(out, corTreeClone(corRest.kallocP, srcModified));
    }

    int triggerMask = (itemP->triggerMask != 0) ? itemP->triggerMask : LD_TRIGGER_DEFAULT;
    // Emit per-attribute null markers (and showChanges previousX) when
    // attributeDeleted is in the trigger mask OR the subscription uses
    // showChanges (§ 5.2.14.1) — entityDeleted+showChanges still wants
    // each attribute rendered with its previous value (ETSI 046_37..39).
    if (((triggerMask & LD_TRIGGER_ATTR_DELETED) != 0) || itemP->showChanges)
    {
      for (CorNode* attrP = entityP->value.head; attrP != NULL; attrP = attrP->next)
      {
        if (attrP->name == NULL || attrP->type != CorObject) continue;
        if (ldIsEntityMember(attrP))                 continue;

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
        CorNode* anyInstP = attrP->value.head;
        if (anyInstP != NULL && anyInstP->type == CorObject)
        {
          CorNode* tNodeP = corTreeLookup(anyInstP, "type");
          if (tNodeP != NULL && tNodeP->type == CorString)
            typeStr = tNodeP->value.s;
        }

        CorNode* nullAttr = corTreeObject(corRest.kallocP, attrP->name);
        corTreeChildAdd(nullAttr, corTreeString(corRest.kallocP, "type", typeStr));
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
        corTreeChildAdd(nullAttr, corTreeString(corRest.kallocP, primaryKey, LD_VOCAB_NGSILD_NULL));

        if (itemP->sysAttrs && anyInstP != NULL && anyInstP->type == CorObject)
        {
          // Per-attribute sysAttrs: pull createdAt/modifiedAt from the
          // pre-delete instance (storage form is CorInt nanoseconds — convert
          // to ISO 8601 here since the entity-delete branch bypasses
          // ldEntityToApi). Attach a fresh deletedAt mirroring the
          // entity-level one.
          CorNode* aCreated = corTreeLookup(anyInstP, LD_VOCAB_CREATED_AT);
          CorNode* aModified = corTreeLookup(anyInstP, LD_VOCAB_MODIFIED_AT);
          if (aCreated != NULL && aCreated->type == CorInt)
          {
            char iso[48];
            nsToIsoLocal((uint64_t) aCreated->value.i, iso, sizeof(iso));
            corTreeChildAdd(nullAttr, corTreeString(corRest.kallocP, LD_VOCAB_CREATED_AT, iso));
          }
          else if (aCreated != NULL)
            corTreeChildAdd(nullAttr, corTreeClone(corRest.kallocP, aCreated));
          if (aModified != NULL && aModified->type == CorInt)
          {
            char iso[48];
            nsToIsoLocal((uint64_t) aModified->value.i, iso, sizeof(iso));
            corTreeChildAdd(nullAttr, corTreeString(corRest.kallocP, LD_VOCAB_MODIFIED_AT, iso));
          }
          else if (aModified != NULL)
            corTreeChildAdd(nullAttr, corTreeClone(corRest.kallocP, aModified));
          if (deletedIso[0] != 0)
            corTreeChildAdd(nullAttr, corTreeString(corRest.kallocP, "deletedAt", deletedIso));
        }

        // showChanges (§ 5.8.6 / § 5.2.14.1): on entity-delete, every
        // attribute carries previous<X> sourced from the pre-delete
        // instance's `value`. Storage normalises typed primary keys to
        // "value", so the previousX label comes from typeStr.
        if (itemP->showChanges && anyInstP != NULL && anyInstP->type == CorObject &&
            (itemP->format != LdFormatSimplified))
        {
          CorNode* preVal = corTreeLookup(anyInstP, "value");
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
            CorNode* prev = corTreeClone(corRest.kallocP, preVal);
            prev->name = (char*) prevKey;
            corTreeChildAdd(nullAttr, prev);
          }
        }

        corTreeChildAdd(out, nullAttr);
      }
    }

    return out;
  }

  CorNode* entityClone = corTreeClone(corRest.kallocP, entityP);

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
  // When the change removed dataset instances, the report lists their dsKeys
  // in "datasetIds" and one marker is injected per instance, so a receiver can
  // tell exactly which instances went away (§ 10.5.7: the datasetId has to be
  // provided when the deleted instance carries one).
  //
  if (op == LdNotifyEntityUpdate && reportP != NULL && reportP->changes != NULL)
  {
    CorNode*  modAtP    = corTreeLookup(entityClone, LD_VOCAB_MODIFIED_AT);
    long long deletedNs = (modAtP != NULL && modAtP->type == CorInt) ? modAtP->value.i : 0;

    for (CorNode* chP = reportP->changes->value.head; chP != NULL; chP = chP->next)
    {
      CorNode* reasonP = corTreeLookup(chP, "reason");
      CorNode* attrP  = corTreeLookup(chP, "attr");
      if (reasonP == NULL || reasonP->type != CorString) continue;
      if (attrP   == NULL || attrP->type   != CorString) continue;
      if (strcmp(reasonP->value.s, "attributeDeleted") != 0) continue;

      // "datasetIds" lists the dsKey of every instance the change removed.
      CorNode* dsKeysP = corTreeLookup(chP, "datasetIds");
      if ((dsKeysP != NULL) && ((dsKeysP->type != CorArray) || (dsKeysP->value.head == NULL)))
        dsKeysP = NULL;

      CorNode* existingAttr = corTreeLookup(entityClone, attrP->value.s);

      // Determine the attribute type. preValue (when present) is the
      // pre-merge wrapper; otherwise read from a surviving instance.
      const char* typeStr = "Property";
      CorNode*    preValP = corTreeLookup(chP, "preValue");
      if (preValP != NULL && preValP->type == CorObject && preValP->value.head != NULL)
      {
        CorNode* anyInstP = preValP->value.head;
        CorNode* tNodeP  = (anyInstP->type == CorObject) ? corTreeLookup(anyInstP, "type") : NULL;
        if (tNodeP != NULL && tNodeP->type == CorString)
          typeStr = tNodeP->value.s;
      }
      else if (existingAttr != NULL && existingAttr->type == CorObject && existingAttr->value.head != NULL)
      {
        CorNode* anyInstP = existingAttr->value.head;
        CorNode* tNodeP  = (anyInstP->type == CorObject) ? corTreeLookup(anyInstP, "type") : NULL;
        if (tNodeP != NULL && tNodeP->type == CorString)
          typeStr = tNodeP->value.s;
      }

      // Build the deleted-instance node: { type, value/lmap=null, [deletedAt] }.
      CorNode* inst = corTreeObject(corRest.kallocP, NULL);
      corTreeChildAdd(inst, corTreeString(corRest.kallocP, "type", typeStr));
      // Per § 5.8.6, LanguageProperty deletion uses `languageMap:
      // {"@none": "urn:ngsi-ld:null"}`, not the bare null marker. All
      // other types put the null marker directly as the value.
      if (strcmp(typeStr, "LanguageProperty") == 0)
      {
        CorNode* lmap = corTreeObject(corRest.kallocP, "value");
        corTreeChildAdd(lmap, corTreeString(corRest.kallocP, "@none", LD_VOCAB_NGSILD_NULL));
        corTreeChildAdd(inst, lmap);
      }
      else
      {
        corTreeChildAdd(inst, corTreeString(corRest.kallocP, "value", LD_VOCAB_NGSILD_NULL));
      }
      if (itemP->sysAttrs == true && deletedNs != 0)
        corTreeChildAdd(inst, corTreeInteger(corRest.kallocP, LD_VOCAB_DELETED_AT, deletedNs));

      if (dsKeysP != NULL)
      {
        // One null marker per removed instance, each keyed by its dsKey
        // ("@none" for the default instance). Surviving instances, if any,
        // are already in the wrapper - the markers join them and the array
        // form rendered downstream carries a datasetId on each keyed entry.
        for (CorNode* keyP = dsKeysP->value.head; keyP != NULL; keyP = keyP->next)
        {
          if (keyP->type != CorString)
            continue;

          CorNode* marker = corTreeClone(corRest.kallocP, inst);
          marker->name   = keyP->value.s;

          if (existingAttr == NULL)
          {
            existingAttr = corTreeObject(corRest.kallocP, attrP->value.s);
            corTreeChildAdd(entityClone, existingAttr);
          }
          corTreeChildAdd(existingAttr, marker);
        }
      }
      else if (existingAttr == NULL)
      {
        // Whole-attribute deletion reported without instance detail (batch
        // and merge paths): a single @none instance carrying the marker.
        inst->name = (char*) "@none";
        CorNode* wrapper = corTreeObject(corRest.kallocP, attrP->value.s);
        corTreeChildAdd(wrapper, inst);
        corTreeChildAdd(entityClone, wrapper);
      }
      // else: attribute still present and no instance detail — nothing to inject.
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
    CorNode* attrP = entityClone->value.head;
    while (attrP != NULL)
    {
      CorNode* nextAttr = attrP->next;

      if (attrP->type != CorObject || attrP->name == NULL ||
          strcmp(attrP->name, "id") == 0 || strcmp(attrP->name, "type") == 0)
      {
        attrP = nextAttr;
        continue;
      }

      CorNode* instP = attrP->value.head;
      while (instP != NULL)
      {
        CorNode* nextInst = instP->next;
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
          corTreeChildRemove(attrP, instP);

        instP = nextInst;
      }

      if (attrP->value.head == NULL)
        corTreeChildRemove(entityClone, attrP);

      attrP = nextAttr;
    }
  }

  ldEntityToApi(entityClone, &corRest.kalloc);
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
      (itemP->format != LdFormatSimplified))
  {
    for (CorNode* chP = reportP->changes->value.head; chP != NULL; chP = chP->next)
    {
      CorNode* attrNameP = corTreeLookup(chP, "attr");
      CorNode* preValueP = corTreeLookup(chP, "preValue");
      if (attrNameP == NULL || attrNameP->type != CorString) continue;
      if (preValueP == NULL) continue;

      // preValue is the pre-change dataset-keyed wrapper
      // ({"@none":{type:..,value:..}, ...}). Storage form keeps the raw
      // value under "value" regardless of attr type — pick the right
      // previousX key based on the per-instance "type".
      CorNode* preInst = corTreeLookup(preValueP, "@none");
      if (preInst == NULL && preValueP->type == CorObject)
        preInst = preValueP->value.head;
      if (preInst == NULL || preInst->type != CorObject) continue;

      CorNode* preVal = corTreeLookup(preInst, "value");
      CorNode* preType = corTreeLookup(preInst, "type");
      if (preVal == NULL) continue;

      const char* prevKey = "previousValue";
      if (preType != NULL && preType->type == CorString && preType->value.s != NULL)
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

      CorNode* attrOutP = corTreeLookup(entityClone, attrNameP->value.s);
      if (attrOutP == NULL)
      {
        // attribute was deleted from entity — add a minimal wrapper
        // carrying only the previousX marker so showChanges sees it.
        attrOutP = corTreeObject(corRest.kallocP, attrNameP->value.s);
        corTreeChildAdd(entityClone, attrOutP);
        if (preType != NULL) corTreeChildAdd(attrOutP, corTreeClone(corRest.kallocP, preType));
      }
      if (attrOutP->type != CorObject) continue;

      CorNode* c = corTreeClone(corRest.kallocP, preVal);
      c->name = (char*) prevKey;
      corTreeChildAdd(attrOutP, c);
    }
  }

  if (itemP->notifAttrsV != NULL)
  {
    CorNode* childP = entityClone->value.head;
    while (childP != NULL)
    {
      CorNode* nextP = childP->next;

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
          corTreeChildRemove(entityClone, childP);
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
    ldLangReduce(entityClone, itemP->lang, &corRest.kalloc);

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
    uint64_t cool = (itemP->cooldownNs != 0) ? itemP->cooldownNs : ldDefaultCooldownNs;
    if (itemP->lastFailure + cool > corRest.requestStartTime)
      return;
  }

  char isoTimeBuf[64];
  isoNow(isoTimeBuf, sizeof(isoTimeBuf));

  CorNode* notification = corTreeObject(corRest.kallocP, NULL);
  corTreeChildAdd(notification, corTreeString(corRest.kallocP, "id",  notifIdGenerate()));
  corTreeChildAdd(notification, corTreeString(corRest.kallocP, "type", "Notification"));
  corTreeChildAdd(notification, corTreeString(corRest.kallocP, "subscriptionId", itemP->subId));
  corTreeChildAdd(notification, corTreeString(corRest.kallocP, "notifiedAt", isoTimeBuf));

  CorNode* dataArray = corTreeArray(corRest.kallocP, "data");
  for (int i = 0; i < n; i++)
    corTreeChildAdd(dataArray, buildNotifDataEntry(itemP, entries[i]));
  corTreeChildAdd(notification, dataArray);

  // § 5.2.14 notification.join — linked-entity retrieval (§ 4.5.23).
  // Hook is installed by the broker (it owns db.* and the reg cache);
  // a no-op when the broker hasn't registered or join is "@none".
  if (itemP->notifJoinActive)
  {
    int level = (itemP->notifJoinLevel > 0) ? itemP->notifJoinLevel : 1;
    ldLinkedEntitiesHookInvoke(dataArray, itemP->notifJoin, level, itemP->sysAttrs, corNgsild.tenantP);
  }

  // Format conversion (deferred from buildNotifDataEntry so the linked-
  // entity hook runs first). simplified/keyValues read `entity` off a
  // Relationship when join=inline attached it — if simplify ran before
  // the join hook, the Relationship would already be collapsed to its
  // URI and the inlined Entity dropped on the floor.
  //
  // The very same renderers the GET/query path uses. There used to be a second
  // pair here (ldConciseEntity/ldSimplifyEntity), and the two copies had drifted:
  // only this side implemented § 5.3.2.4's multi-attribute `dataset` map, and
  // neither concise copy implemented § 5.3.2.3 step 1 - so one entity rendered
  // two different shapes depending on whether it was fetched or notified.
  //
  if (itemP->format != LdFormatNone)
  {
    for (CorNode* eP = dataArray->value.head; eP != NULL; eP = eP->next)
    {
      if (itemP->format == LdFormatSimplified)
        ldToSimplified(eP, &corRest.kalloc);
      else if (itemP->format == LdFormatConcise)
        ldToConcise(eP, &corRest.kalloc);
    }
  }

  // Compact expanded URIs to short names (covers every data[] entry).
  // Use the sub's jsonldContext when set so user-vocabulary terms (e.g.
  // "Building", "name") compact too — without it only core-context names
  // (id/type/...) collapse and the body ships as expanded IRIs.
  CorLdContext* notifCtx = NULL;
  if (itemP->contextUrl != NULL)
    notifCtx = corLdContextFromUrl(itemP->contextUrl, &corRest.kalloc);
  if (notifCtx != NULL)
    corLdCompactTreeWith(notification, notifCtx);
  else
    corLdCompactTree(notification);

  // § 5.2.12 / § 4.3.6.8: backwards-compat downgrade of entity payloads
  // when the subscription requested an older NGSI-LD version.
  if (itemP->conformanceMajor != 0 || itemP->conformanceMinor != 0)
  {
    CorNode* dataP = corTreeLookup(notification, "data");
    if (dataP != NULL)
      ldConformanceDowngrade(dataP, itemP->conformanceMajor, itemP->conformanceMinor, corRest.kallocP);
  }

  // § 5.2.14: when endpoint.accept is application/geo+json, replace the
  // `data` array with a FeatureCollection. Each entity becomes a Feature
  // with id at the top level, geometry from the GeoProperty, and the rest
  // of the entity as `properties`.
  bool acceptGeoJson = (itemP->endpointAccept == CorMimeGeoJson);
  bool acceptLdJson  = (itemP->endpointAccept == CorMimeLdJson);

  // ld+json notification body: per § 5.8.6 + § 6.3.5, each entity in data[]
  // (and the FeatureCollection / each Feature for geo+json — § 6.3.7) shall
  // carry @context inline. Plain application/json puts @context in the Link
  // header only; we emit that further down regardless of body shape.
  // ETSI 046_19, 046_20.
  // § 6.5.2: a receiverInfo Prefer "body=json" moves the geo+json @context from
  // the body to the Link header (validated geo+json-only in ldCheckSubscription).
  bool notifPreferBodyJson = false;
  if (acceptGeoJson && itemP->receiverInfo != NULL && itemP->receiverInfo->type == CorArray)
  {
    for (CorNode* kvP = itemP->receiverInfo->value.head; kvP != NULL; kvP = kvP->next)
    {
      if (kvP->type != CorObject) continue;
      CorNode* kP = corTreeLookup(kvP, "key");
      CorNode* vP = corTreeLookup(kvP, "value");
      if (kP != NULL && kP->type == CorString && vP != NULL && vP->type == CorString && strcasecmp(kP->value.s, "Prefer") == 0)
      {
        const char* p = strstr(vP->value.s, "body=");
        if (p != NULL && strncmp(p + 5, "json", 4) == 0 && (p[9] == 0 || p[9] == ';' || p[9] == ' ' || p[9] == ','))
          notifPreferBodyJson = true;
        break;
      }
    }
  }

  // @context placement: in the body for ld+json (inline per entity) and for
  // geo+json unless Prefer body=json; otherwise via the Link header only (plain
  // json, or geo+json with Prefer body=json).
  bool ctxInBody = acceptLdJson || (acceptGeoJson && !notifPreferBodyJson);

  // ld+json: @context inline on each entity in data[] (§ 5.8.6 / § 6.3.5).
  if (acceptLdJson && itemP->contextUrl != NULL)
  {
    CorNode* dataP = corTreeLookup(notification, "data");
    if (dataP != NULL && dataP->type == CorArray)
    {
      for (CorNode* ep = dataP->value.head; ep != NULL; ep = ep->next)
      {
        if (ep->type == CorObject && corTreeLookup(ep, "@context") == NULL)
          corTreeChildAdd(ep, corTreeString(corRest.kallocP, "@context", itemP->contextUrl));
      }
    }
  }

  if (acceptGeoJson)
  {
    CorNode* oldDataP = corTreeLookup(notification, "data");
    if (oldDataP != NULL && oldDataP->type == CorArray)
    {
      CorNode* newDataP = oldDataP;
      ldToGeoJson(&newDataP, NULL /* default "location" */, corRest.kallocP);
      if (newDataP != NULL && newDataP != oldDataP)
      {
        newDataP->name = (char*) "data";
        corTreeChildReplace(notification, oldDataP, newDataP);
      }
      // § 5.2.6.11.2: ONE @context as a top-level FeatureCollection member
      // (an RFC 7946 § 6.1 foreign member) when @context belongs in the body —
      // not copied into each Feature, and no Link header (see below).
      if (ctxInBody && itemP->contextUrl != NULL && newDataP != NULL &&
          newDataP->type == CorObject && corTreeLookup(newDataP, "@context") == NULL)
        corTreeChildAdd(newDataP, corTreeString(corRest.kallocP, "@context", itemP->contextUrl));
    }
  }

  //
  // Render to JSON
  //
  int   bodySize = corJsonFastRenderSize(notification) + 1;
  char* body     = (char*) corAlloc(&corRest.kalloc, bodySize);

  corJsonFastRender(notification, body);

  //
  // Compute Link header — needed for both HTTP and MQTT paths.
  //
  const char* ctxUrl = itemP->contextUrl;
  if (ctxUrl == NULL)
  {
    CorLdContext* coreP = corLdCoreContext();
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
  // Content-Type of the notification — the single resolved media type for this
  // subscription (itemP->endpointAccept, decided once in ldSubCache from
  // endpoint.accept or, absent that, the receiverInfo Content-Type), rendered
  // via the enum→string table. No re-derivation from accept/receiverInfo here.
  //   application/ld+json  → JSON-LD (Link header omitted; @context inline if any)
  //   application/geo+json → GeoJSON FeatureCollection (already converted above)
  //   default              → application/json (Link header carries @context)
  //
  const char* contentType = corMimeString(itemP->endpointAccept);

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
    itemP->lastNotification = corRest.requestStartTime;
    if (ok)
      itemP->lastSuccess = corRest.requestStartTime;
    else
    {
      itemP->timesFailed += 1;
      itemP->lastFailure  = corRest.requestStartTime;
    }
    ldNotifyStatsHookInvoke(false /*csrSub*/, ok);
    return;
  }

  //
  // Send HTTP POST
  //
  CorRestClientRequest  req;
  CorRestClientResponse resp;

  corRestClientRequestInit(&req, CorVerbPost, itemP->endpointUri, NULL);
  corRestClientRequestHeader(&req, "Content-Type", contentType);

  // § 6.4.8 — a notification resulting from a subscription matched under a
  // non-default tenant carries the NGSILD-Tenant header (the same tenant the
  // consumer addressed); without it the entity ids in the body are ambiguous.
  // Taken from the triggering request (the change happened in the sub's tenant).
  for (int i = 0; i < corRest.in.httpHeaderCount; i++)
  {
    if ((corRest.in.httpHeaderV[i].key != NULL) &&
        (strcasecmp(corRest.in.httpHeaderV[i].key, "NGSILD-Tenant") == 0) &&
        (corRest.in.httpHeaderV[i].value != NULL) && (corRest.in.httpHeaderV[i].value[0] != 0))
    {
      corRestClientRequestHeader(&req, "NGSILD-Tenant", corRest.in.httpHeaderV[i].value);
      break;
    }
  }

  // Ngsild-Attribute-Format — non-default representation format of the
  // notification data, as the lowercase NGSI-LD format value (concise /
  // simplified). The default (normalized) carries no header, and the v2 synonym
  // "keyValues" is normalised to "simplified". (A non-standard extension;
  // pending addition to TS 104-176.)
  if (itemP->format != LdFormatNone && itemP->format != LdFormatNormalized)
    corRestClientRequestHeader(&req, "Ngsild-Attribute-Format", ldFormatToString(itemP->format));

  // The Link header carries @context ONLY when it is not already in the body
  // (§ 5.8.6 / § 6.3.5): plain json, or geo+json with Prefer body=json. For
  // ld+json and default geo+json the @context is inline, so a Link would be a
  // duplicate (046_14_01 asserts its absence).
  if (linkBuf[0] != 0 && !ctxInBody)
    corRestClientRequestHeader(&req, "Link", linkBuf);

  // § 5.2.15 endpoint.receiverInfo — emit each {key,value} as a request header
  if (itemP->receiverInfo != NULL && itemP->receiverInfo->type == CorArray)
  {
    for (CorNode* kvP = itemP->receiverInfo->value.head; kvP != NULL; kvP = kvP->next)
    {
      if (kvP->type != CorObject) continue;
      CorNode* kP = corTreeLookup(kvP, "key");
      CorNode* vP = corTreeLookup(kvP, "value");
      if (kP != NULL && kP->type == CorString && vP != NULL && vP->type == CorString)
      {
        // Content-Type and an @context Link are absorbed into endpointAccept /
        // contextUrl (ldSubCache) and emitted once by the broker above. Prefer
        // is a broker-side rendering directive (geo+json @context placement),
        // not receiver information — skip all three so the notification carries
        // exactly one Content-Type / Link and no stray Prefer (the rest, e.g.
        // Authorization, pass through). (Prefer-in-receiverInfo per § 6.5.2 — but
        // we do not leak it to the receiver; see spec-doubt #101.)
        // NGSILD-Tenant is broker-managed (§ 6.4.8 / § 6.5.2: "cannot be
        // overridden") — the broker emits it above from the triggering request.
        if ((strcasecmp(kP->value.s, "Content-Type") == 0) ||
            (strcasecmp(kP->value.s, "Prefer") == 0) ||
            (strcasecmp(kP->value.s, "NGSILD-Tenant") == 0) ||
            ((strcasecmp(kP->value.s, "Link") == 0) && (strstr(vP->value.s, "json-ld#context") != NULL)))
          continue;

        const char* hv = ldRequestSubstitute(kP->value.s, vP->value.s);
        if (hv != NULL)
          corRestClientRequestHeader(&req, kP->value.s, hv);
      }
    }
  }

  corRestClientRequestBody(&req, body, strlen(body));
  // § 5.2.15 endpoint.timeout — per-sub override; default 10s
  int reqTmoMs = (itemP->timeoutMs > 0) ? itemP->timeoutMs : 10000;
  corRestClientRequestTimeout(&req, 5000, reqTmoMs);

  // Trace the outgoing notification (each aspect on its own level).
  {
    const char* nq       = (req.url != NULL) ? strchr(req.url, '?') : NULL;
    int         nPathLen  = (nq != NULL) ? (int)(nq - req.url) : (req.url ? (int) strlen(req.url) : 0);
    COR_T(LdTNotifReq, "notification request: %s %.*s", corRestVerbToString(req.verb), nPathLen, req.url ? req.url : "");
    for (const char* p = (nq != NULL) ? nq + 1 : NULL; p != NULL && *p != 0; )
    {
      const char* amp = strchr(p, '&');
      int         len = (amp != NULL) ? (int)(amp - p) : (int) strlen(p);
      COR_T(LdTNotifReqParam, "notification request param: %.*s", len, p);
      if (amp == NULL) break;
      p = amp + 1;
    }
    for (int h = 0; h < req.headerCount; h++)
      COR_T(LdTNotifHeader, "notification request header: %s: %s", req.headerV[h].key, req.headerV[h].value ? req.headerV[h].value : "");
    COR_T(LdTNotifBody, "notification request body (%zu bytes): %s", strlen(body), body);
  }

  corRestClientSend(&req, &resp);

  COR_T(LdTNotifRes, "notification response: status %d", resp.statusCode);

  //
  // Update notification counters (use request timestamp, nanoseconds)
  //
  itemP->timesSent       += 1;
  itemP->lastNotification = corRest.requestStartTime;

  bool ok = (resp.statusCode >= 200 && resp.statusCode < 300);
  if (ok)
    itemP->lastSuccess = corRest.requestStartTime;
  else
  {
    itemP->timesFailed += 1;
    itemP->lastFailure  = corRest.requestStartTime;
  }

  corRestClientResponseCleanup(&resp);
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
  // Concurrency: the cache is shared and a sub DELETE could free an item mid-
  // walk. So we MATCH under the rdlock and PIN each sub that has matches into a
  // send list, then drop the lock and SEND lock-free (sends are slow — can't
  // hold the cache lock across them), unpinning afterwards.
  //
  typedef struct { LdSubCacheItem* itemP; LdNotifyPendingEntry** matched; int matchedN; } SendEntry;

  ldSubCacheRdLock(cacheP);

  int subCount = 0;
  for (LdSubCacheItem* c = cacheP->itemList; c != NULL; c = c->next)
    subCount++;
  SendEntry* sendV = (subCount > 0) ? (SendEntry*) corAlloc(&corRest.kalloc, subCount * sizeof(SendEntry)) : NULL;
  int        sendN = 0;

  for (LdSubCacheItem* itemP = cacheP->itemList; itemP != NULL; itemP = itemP->next)
  {
    //
    // Per-sub static checks (done once, regardless of pending count)
    //
    if (itemP->status == LdSubStatusPaused || itemP->status == LdSubStatusExpired)
      continue;

    if (itemP->expiresAt > 0 && corRest.requestStartTime > itemP->expiresAt)
    {
      itemP->status = LdSubStatusExpired;
      continue;
    }

    // § 5.2.x throttling — a throttled sub does NOT send from this (synchronous,
    // change-driven) path. Each match is BUFFERED into the coalesce-to-latest
    // dirty set; the periodic flush is the sole sender, so a burst's final state
    // is never dropped (the old behaviour did `continue` here = tail-loss) and
    // there is no sync-send vs flush race on the same item.
    bool throttled = (itemP->throttling > 0);

    //
    // Per-pending match pass
    //
    LdNotifyPendingEntry** matched  = (LdNotifyPendingEntry**) corAlloc(&corRest.kalloc, pendingN * sizeof(LdNotifyPendingEntry*));
    int                    matchedN = 0;

    for (int i = 0; i < pendingN; i++)
    {
      LdNotifyPendingEntry* p = &pendingV[i];
      if (p->entityP == NULL)
        continue;

      LdMergeReport* reportP = p->hasReport ? &p->report : NULL;

      CorNode*    entityIdP   = corTreeLookup(p->entityP, "id");
      CorNode*    entityTypeP = corTreeLookup(p->entityP, "type");
      const char* entityId    = (entityIdP != NULL && entityIdP->type == CorString) ? entityIdP->value.s : NULL;

      if (!triggerMatches(itemP, p->op, p->reasonsMask))
        continue;

      if (!entitiesMatch(itemP, entityId, entityTypeP))
        continue;

      if (!watchedAttrsMatch(itemP, p->entityP, p->op, reportP))
        continue;

      if (itemP->scopeExpr != NULL)
      {
        CorNode* scopeP = corTreeLookup(p->entityP, LD_VOCAB_SCOPE);
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

      if (throttled)
        ldThrottleDirtyUpsert(itemP, entityId, p->reasonsMask, p->op, p->deletedAtNs, p->entityP);
      else
        matched[matchedN++] = p;
    }

    if (matchedN > 0 && sendV != NULL)
    {
      sendV[sendN].itemP    = itemP;
      sendV[sendN].matched  = matched;
      sendV[sendN].matchedN = matchedN;
      ldSubCacheItemPin(itemP);    // keep alive across the (slow, lock-free) send
      sendN++;
    }
  }

  ldSubCacheUnlock(cacheP);

  // Send lock-free; the items are pinned so a concurrent sub DELETE can't free
  // them out from under us (it parks them on retiredList until we unpin).
  for (int s = 0; s < sendN; s++)
  {
    notificationSendMany(sendV[s].itemP, sendV[s].matched, sendV[s].matchedN);
    ldSubCacheItemUnpin(sendV[s].itemP);
  }
}



// -----------------------------------------------------------------------------
//
// § 5.2.x throttling — coalesce-to-latest flush
//
// A throttled subscription never sends from the synchronous change-driven path
// (ldSubscriptionNotifyBatch buffers its matches into the dirty set). This
// periodic flush is its SOLE sender: once per second it walks the sub cache and,
// for every throttled sub whose window has elapsed and whose dirty set is
// non-empty, sends ONE coalesced notification carrying the LATEST state of each
// buffered entity. Updates are re-queried at flush (so the body is current and
// the buffer stays O(1)/update); a buffered DELETE carries the state captured at
// delete time (a gone entity can't be re-queried).
//
static LdThrottleRetrieveFunc throttleRetrieveFn = NULL;



//
// throttleFlushTick - registered with the periodic-dispatch engine (1 Hz).
//
static void throttleFlushTick(void* ctx, uint64_t now, CorAlloc* kaP)
{
  (void) kaP;
  LdSubCache* cacheP = (LdSubCache*) ctx;
  if (cacheP == NULL)
    return;

  // Phase 1 — under the rdlock, pin every throttled sub that is due to flush.
  // (dirtyN is read without dirtyLock: a benign gate — a missed/extra item is
  // caught next tick or drains to 0 below.)
  ldSubCacheRdLock(cacheP);

  int subCount = 0;
  for (LdSubCacheItem* c = cacheP->itemList; c != NULL; c = c->next)
    subCount++;

  LdSubCacheItem** dueV = (subCount > 0) ? (LdSubCacheItem**) corAlloc(&corRest.kalloc, subCount * sizeof(LdSubCacheItem*)) : NULL;
  int              dueN = 0;

  for (LdSubCacheItem* itemP = cacheP->itemList; itemP != NULL && dueV != NULL; itemP = itemP->next)
  {
    if (itemP->throttling <= 0 || itemP->dirtyN == 0)
      continue;
    if (itemP->status == LdSubStatusPaused || itemP->status == LdSubStatusExpired)
      continue;

    uint64_t throttlingNs = (uint64_t) (itemP->throttling * 1e9);
    if (itemP->lastNotification + throttlingNs > now)
      continue;

    ldSubCacheItemPin(itemP);
    dueV[dueN++] = itemP;
  }

  ldSubCacheUnlock(cacheP);

  // Phase 2 — lock-free (items pinned): drain each due sub and send one
  // coalesced notification with the latest state of every buffered entity.
  for (int d = 0; d < dueN; d++)
  {
    LdSubCacheItem*  itemP    = dueV[d];
    LdThrottleEntry* entriesV = NULL;
    int              entriesN = 0;

    ldThrottleDirtyDrain(itemP, &entriesV, &entriesN);

    if (entriesN == 0)   // raced empty since the Phase-1 gate
    {
      ldThrottleDirtyEntriesFree(entriesV, entriesN);
      ldSubCacheItemUnpin(itemP);
      continue;
    }

    LdNotifyPendingEntry*  peArr  = (LdNotifyPendingEntry*)  corAlloc(&corRest.kalloc, entriesN * sizeof(LdNotifyPendingEntry));
    LdNotifyPendingEntry** pePtrs = (LdNotifyPendingEntry**) corAlloc(&corRest.kalloc, entriesN * sizeof(LdNotifyPendingEntry*));
    int                    peN    = 0;

    for (int i = 0; i < entriesN; i++)
    {
      LdThrottleEntry* e     = &entriesV[i];
      CorNode*         state = NULL;
      LdNotifyOp       op    = LdNotifyEntityUpdate;
      uint64_t         delNs = 0;

      if (e->op == LdNotifyEntityDelete)
      {
        state = e->deleteState;        // captured at delete time (can't re-query a gone entity)
        op    = LdNotifyEntityDelete;
        delNs = e->deletedAtNs;
      }
      else
      {
        state = (throttleRetrieveFn != NULL) ? throttleRetrieveFn(e->entityId, &corRest.kalloc) : NULL;
        if (state == NULL)             // vanished without a delete record — skip
          continue;
      }

      peArr[peN].entityP     = state;
      peArr[peN].op          = op;
      peArr[peN].hasReport   = false;
      memset(&peArr[peN].report, 0, sizeof(peArr[peN].report));
      peArr[peN].reasonsMask = e->reasonsMask;
      peArr[peN].deletedAtNs = delNs;
      pePtrs[peN] = &peArr[peN];
      peN++;
    }

    if (peN > 0)
    {
      notificationSendMany(itemP, pePtrs, peN);
      itemP->lastNotification = now;
    }

    ldThrottleDirtyEntriesFree(entriesV, entriesN);
    ldSubCacheItemUnpin(itemP);
  }
}



//
// ldThrottleFlushStart - register the coalesce-to-latest flush with the
// periodic loop. retrieveFn is the broker's "retrieve one entity by id" hook
// (the lib has no DB access). Single-tenant (tenant0) for now, like the other
// periodic ticks.
//
void ldThrottleFlushStart(LdSubCache* cacheP, LdThrottleRetrieveFunc retrieveFn)
{
  throttleRetrieveFn = retrieveFn;
  ldPeriodicLoopRegister(throttleFlushTick, cacheP);
}
