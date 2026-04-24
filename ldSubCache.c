//
// FILE            ldSubCache.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Subscription cache operations.
//
#include <regex.h>                                     // regcomp, regfree
#include <stdlib.h>                                    // malloc, calloc, free
#include <string.h>                                    // strcmp, strdup

#include "kalloc/kaBufferInit.h"                       // kaBufferInit
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjClone.h"                             // kjClone
#include "kjson/kjFree.h"                              // kjFree
#include "kjson/kjLookup.h"                            // kjLookup
#include "kjson/kjBuilder.h"                           // kjChildRemove

#include "swNgsild/LdVocab.h"                          // LD_VOCAB_*
#include "swNgsild/LdSubCache.h"                       // LdSubCache, LdSubCacheItem
#include "swNgsild/LdScopeExpr.h"                      // ldScopeExprParse
#include "swNgsild/LdGeoRel.h"                         // ldGeoRelParse
#include "swNgsild/ldCheckDateTime.h"                  // ldCheckDateTime
#include "swJsonld/swldExpand.h"                       // swldExpand, swldAlreadyExpanded
#include "swNgsild/ldSubscriptionNotify.h"              // ldTriggerFromString
#include "swNgsild/ldQParse.h"                         // ldQParse
#include "swNgsild/ldSubCache.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// entitySelectorsExtract - parse the entities[] array into a linked list
//
static LdSubEntitySelector* entitySelectorsExtract(KjNode* entitiesP)
{
  if (entitiesP == NULL || entitiesP->type != KjArray)
    return NULL;

  LdSubEntitySelector* head = NULL;
  LdSubEntitySelector* tail = NULL;

  for (KjNode* selP = entitiesP->value.firstChildP; selP != NULL; selP = selP->next)
  {
    if (selP->type != KjObject)
      continue;

    LdSubEntitySelector* esP = (LdSubEntitySelector*) calloc(1, sizeof(LdSubEntitySelector));

    // type (borrowed pointer into the cloned subTree)
    KjNode* typeP = kjLookup(selP, "type");
    if (typeP != NULL && typeP->type == KjString)
      esP->type = typeP->value.s;

    // id (borrowed pointer)
    KjNode* idP = kjLookup(selP, "id");
    if (idP != NULL && idP->type == KjString)
      esP->id = idP->value.s;

    // idPattern — compile regex
    KjNode* patP = kjLookup(selP, LD_VOCAB_ID_PATTERN);
    if (patP != NULL && patP->type == KjString)
    {
      LdSubIdPattern* ripP = (LdSubIdPattern*) calloc(1, sizeof(LdSubIdPattern));

      if (regcomp(&ripP->regex, patP->value.s, REG_EXTENDED | REG_NOSUB) == 0)
      {
        esP->idPatternList = ripP;
      }
      else
      {
        free(ripP);
      }
    }

    // Append to linked list
    if (tail == NULL)
      head = esP;
    else
      tail->next = esP;
    tail = esP;
  }

  return head;
}



// -----------------------------------------------------------------------------
//
// watchedAttrsExtract - build NULL-terminated string array from watchedAttributes
//
// Returns NULL if watchedAttributes is absent (meaning all attributes are watched).
// Strings are borrowed pointers into the cloned subTree.
//
static char** watchedAttrsExtract(KjNode* watchedP)
{
  if (watchedP == NULL || watchedP->type != KjArray)
    return NULL;

  // Count elements
  int count = 0;
  for (KjNode* wP = watchedP->value.firstChildP; wP != NULL; wP = wP->next)
    if (wP->type == KjString)
      count++;

  if (count == 0)
    return NULL;

  char** v = (char**) malloc((count + 1) * sizeof(char*));
  int ix = 0;

  for (KjNode* wP = watchedP->value.firstChildP; wP != NULL; wP = wP->next)
  {
    if (wP->type == KjString)
      v[ix++] = wP->value.s;  // borrowed pointer
  }

  v[ix] = NULL;
  return v;
}





// -----------------------------------------------------------------------------
//
// entitySelectorsFree - free a linked list of entity selectors
//
static void entitySelectorsFree(LdSubEntitySelector* head)
{
  while (head != NULL)
  {
    LdSubEntitySelector* next = head->next;

    // Free compiled regexes
    LdSubIdPattern* ripP = head->idPatternList;
    while (ripP != NULL)
    {
      LdSubIdPattern* ripNext = ripP->next;
      regfree(&ripP->regex);
      free(ripP);
      ripP = ripNext;
    }

    free(head);
    head = next;
  }
}



// -----------------------------------------------------------------------------
//
// ldSubCacheCreate -
//
LdSubCache* ldSubCacheCreate(void)
{
  LdSubCache* cacheP = (LdSubCache*) calloc(1, sizeof(LdSubCache));

  // Initialize persistent allocator for parsed q/scope trees.
  // Initial buffer is inline (1024 bytes); overflow allocates via calloc (4096 chunks).
  kaBufferInit(&cacheP->alloc, cacheP->allocBuf, sizeof(cacheP->allocBuf), 4096, NULL, "subCache");

  return cacheP;
}



// -----------------------------------------------------------------------------
//
// ldSubCacheItemAdd -
//
LdSubCacheItem* ldSubCacheItemAdd(LdSubCache* cacheP, KjNode* subTree, LdQNode* qExpr)
{
  if (cacheP == NULL || subTree == NULL)
    return NULL;

  LdSubCacheItem* itemP = (LdSubCacheItem*) calloc(1, sizeof(LdSubCacheItem));

  //
  // Clone the subscription tree (malloc allocator — persists across requests)
  //
  itemP->subTree = kjClone(NULL, subTree);

  //
  // Extract subscription ID
  //
  KjNode* idP = kjLookup(itemP->subTree, "id");
  itemP->subId = (idP != NULL && idP->type == KjString) ? strdup(idP->value.s) : NULL;

  //
  // Pre-parse matching fields from the cloned tree
  //
  KjNode* entitiesP = kjLookup(itemP->subTree, LD_VOCAB_ENTITIES);
  itemP->entitySelectors = entitySelectorsExtract(entitiesP);

  KjNode* watchedP = kjLookup(itemP->subTree, LD_VOCAB_WATCHED_ATTRS);
  itemP->watchedAttrsV = watchedAttrsExtract(watchedP);

  // Expand short names in watchedAttrsV — same motivation as notifAttrsV
  // below. JSON-LD value coercion on array entries is intentionally NOT
  // applied by swldExpandTree, so the strings stay short after parseHook.
  // Downstream match paths (entity-side notify, CSR-side notify) compare
  // against expanded IRIs, so expand once here at cache-ingest time.
  if (itemP->watchedAttrsV != NULL)
  {
    for (int i = 0; itemP->watchedAttrsV[i] != NULL; i++)
    {
      if (!swldAlreadyExpanded(itemP->watchedAttrsV[i]))
        itemP->watchedAttrsV[i] = swldExpand(NULL, itemP->watchedAttrsV[i], &cacheP->alloc, NULL, NULL);
    }
  }

  // Use pre-parsed tree if provided, otherwise parse from the stored expanded q-string.
  // Note: ldQParse called here will re-expand attrs via swNgsild.contextP, but since
  // the attrs are already expanded IRIs (contain "://"), swldExpand returns them unchanged.
  if (qExpr != NULL)
  {
    itemP->qExpr = qExpr;
  }
  else
  {
    KjNode* qNodeP = kjLookup(itemP->subTree, "q");
    if (qNodeP != NULL && qNodeP->type == KjString)
      itemP->qExpr = ldQParse(qNodeP->value.s, &cacheP->alloc);
  }

  //
  // geoQ: { georel: "near;maxDistance==1000", geometry: "Point", coordinates: "[-3.7,40.4]", geoproperty: "location" }
  //
  KjNode* geoQP = kjLookup(itemP->subTree, "geoQ");
  if (geoQP != NULL && geoQP->type == KjObject)
  {
    KjNode* georelP   = kjLookup(geoQP, "georel");
    KjNode* geomP     = kjLookup(geoQP, "geometry");
    KjNode* coordsP   = kjLookup(geoQP, "coordinates");
    KjNode* geopropP  = kjLookup(geoQP, "geoproperty");

    if (georelP != NULL && georelP->type == KjString)
      itemP->geoRel = ldGeoRelParse(georelP->value.s, &cacheP->alloc);

    if (geomP != NULL && geomP->type == KjString)
      itemP->geoGeometry = geomP->value.s;  // borrowed pointer

    if (coordsP != NULL && coordsP->type == KjString)
      itemP->geoCoordinates = coordsP->value.s;  // borrowed pointer

    if (geopropP != NULL && geopropP->type == KjString)
    {
      // The value may be a short name (e.g. "location") — expand it
      if (swldAlreadyExpanded(geopropP->value.s))
        itemP->geoProperty = geopropP->value.s;
      else
        itemP->geoProperty = swldExpand(NULL, geopropP->value.s, &cacheP->alloc, NULL, NULL);
    }
    else
    {
      itemP->geoProperty = "https://uri.etsi.org/ngsi-ld/location";  // default
    }
  }

  KjNode* scopeQP = kjLookup(itemP->subTree, "scopeQ");
  if (scopeQP != NULL && scopeQP->type == KjString)
    itemP->scopeExpr = ldScopeExprParse(scopeQP->value.s, &cacheP->alloc);

  KjNode* triggerP = kjLookup(itemP->subTree, "notificationTrigger");
  if (triggerP != NULL && triggerP->type == KjArray)
  {
    itemP->triggerMask = 0;
    for (KjNode* tP = triggerP->value.firstChildP; tP != NULL; tP = tP->next)
    {
      if (tP->type == KjString)
        itemP->triggerMask |= ldTriggerFromString(tP->value.s);
    }
  }
  // else triggerMask stays 0 (calloc'd) → use LD_TRIGGER_DEFAULT in matching

  //
  // Extract state fields (borrowed pointers into cloned tree)
  //
  KjNode* statusP = kjLookup(itemP->subTree, LD_VOCAB_STATUS);
  itemP->status = (statusP != NULL && statusP->type == KjString) ? statusP->value.s : "active";

  KjNode* notifP    = kjLookup(itemP->subTree, LD_VOCAB_NOTIFICATION);
  KjNode* endpointP = (notifP != NULL) ? kjLookup(notifP, LD_VOCAB_ENDPOINT) : NULL;
  KjNode* uriP      = (endpointP != NULL) ? kjLookup(endpointP, LD_VOCAB_URI) : NULL;
  itemP->endpointUri = (uriP != NULL && uriP->type == KjString) ? uriP->value.s : NULL;

  KjNode* notifAttrsP = (notifP != NULL) ? kjLookup(notifP, LD_VOCAB_ATTRIBUTES) : NULL;
  itemP->notifAttrsV = watchedAttrsExtract(notifAttrsP);  // reuse same helper (NULL-term string array)

  // Expand short names in notifAttrsV (values aren't expanded by JSON-LD processing)
  if (itemP->notifAttrsV != NULL)
  {
    for (int i = 0; itemP->notifAttrsV[i] != NULL; i++)
    {
      if (!swldAlreadyExpanded(itemP->notifAttrsV[i]))
        itemP->notifAttrsV[i] = swldExpand(NULL, itemP->notifAttrsV[i], &cacheP->alloc, NULL, NULL);
    }
  }

  // datasetId: list of dataset IDs to include in notifications (NULL = all instances)
  // Values are URIs or "@none" (default instance) — no expansion needed.
  KjNode* datasetIdP = kjLookup(itemP->subTree, LD_VOCAB_DATASET_ID);
  itemP->datasetIdV = watchedAttrsExtract(datasetIdP);  // reuse: builds NULL-term string array

  KjNode* expiresP = kjLookup(itemP->subTree, LD_VOCAB_EXPIRES_AT);
  if (expiresP != NULL && expiresP->type == KjString)
    itemP->expiresAt = ldIsoToNanoseconds(expiresP->value.s);

  KjNode* formatP = (notifP != NULL) ? kjLookup(notifP, LD_VOCAB_FORMAT) : NULL;
  itemP->format = (formatP != NULL && formatP->type == KjString) ? formatP->value.s : NULL;

  if (notifP != NULL)
  {
    KjNode* sysP  = kjLookup(notifP, "sysAttrs");
    KjNode* showP = kjLookup(notifP, "showChanges");
    itemP->sysAttrs    = (sysP  != NULL && sysP->type  == KjBoolean && sysP->value.b  == true);
    itemP->showChanges = (showP != NULL && showP->type == KjBoolean && showP->value.b == true);
  }

  KjNode* jcP = kjLookup(itemP->subTree, "jsonldContext");
  itemP->contextUrl = (jcP != NULL && jcP->type == KjString) ? jcP->value.s : NULL;

  KjNode* throttlingP = kjLookup(itemP->subTree, LD_VOCAB_THROTTLING);
  if (throttlingP != NULL)
  {
    if (throttlingP->type == KjFloat)  itemP->throttling = throttlingP->value.f;
    if (throttlingP->type == KjInt)    itemP->throttling = (double) throttlingP->value.i;
  }

  //
  // Stats fields — present when this item came from a mongo-load (a prior
  // flush persisted them). Extract into cache fields and remove from the
  // stored subTree so nothing downstream produces duplicates.
  //
  if (notifP != NULL && notifP->type == KjObject)
  {
    KjNode* tsP = kjLookup(notifP, "timesSent");
    KjNode* tfP = kjLookup(notifP, "timesFailed");
    KjNode* lnP = kjLookup(notifP, "lastNotification");
    KjNode* lsP = kjLookup(notifP, "lastSuccess");
    KjNode* lfP = kjLookup(notifP, "lastFailure");

    if (tsP != NULL && tsP->type == KjInt) itemP->timesSent   = (int) tsP->value.i;
    if (tfP != NULL && tfP->type == KjInt) itemP->timesFailed = (int) tfP->value.i;
    // last* timestamps are stored as int64 nanoseconds in mongo; tolerate
    // ISO strings for any older persisted docs.
    if (lnP != NULL)
    {
      if      (lnP->type == KjInt)    itemP->lastNotification = (uint64_t) lnP->value.i;
      else if (lnP->type == KjString) itemP->lastNotification = ldIsoToNanoseconds(lnP->value.s);
    }
    if (lsP != NULL)
    {
      if      (lsP->type == KjInt)    itemP->lastSuccess = (uint64_t) lsP->value.i;
      else if (lsP->type == KjString) itemP->lastSuccess = ldIsoToNanoseconds(lsP->value.s);
    }
    if (lfP != NULL)
    {
      if      (lfP->type == KjInt)    itemP->lastFailure = (uint64_t) lfP->value.i;
      else if (lfP->type == KjString) itemP->lastFailure = ldIsoToNanoseconds(lfP->value.s);
    }

    // Seed the "last flushed" watermarks to match what was just loaded —
    // no delta to flush yet on this freshly-loaded item.
    itemP->lastFlushedSent   = itemP->timesSent;
    itemP->lastFlushedFailed = itemP->timesFailed;

    if (tsP != NULL) kjChildRemove(notifP, tsP);
    if (tfP != NULL) kjChildRemove(notifP, tfP);
    if (lnP != NULL) kjChildRemove(notifP, lnP);
    if (lsP != NULL) kjChildRemove(notifP, lsP);
    if (lfP != NULL) kjChildRemove(notifP, lfP);
  }

  //
  // Append to cache linked list
  //
  if (cacheP->last == NULL)
    cacheP->itemList = itemP;
  else
    cacheP->last->next = itemP;
  cacheP->last = itemP;

  return itemP;
}



// -----------------------------------------------------------------------------
//
// ldSubCacheItemLookup -
//
LdSubCacheItem* ldSubCacheItemLookup(LdSubCache* cacheP, const char* subId)
{
  if (cacheP == NULL || subId == NULL)
    return NULL;

  for (LdSubCacheItem* itemP = cacheP->itemList; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->subId != NULL && strcmp(itemP->subId, subId) == 0)
      return itemP;
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// cacheItemFree - free a single cache item and all its resources
//
static void cacheItemFree(LdSubCacheItem* itemP)
{
  if (itemP->subId != NULL)
    free(itemP->subId);

  if (itemP->subTree != NULL)
    kjFree(itemP->subTree);

  entitySelectorsFree(itemP->entitySelectors);

  if (itemP->watchedAttrsV != NULL)
    free(itemP->watchedAttrsV);  // array only — strings are borrowed

  if (itemP->notifAttrsV != NULL)
    free(itemP->notifAttrsV);    // array only — strings are borrowed

  if (itemP->datasetIdV != NULL)
    free(itemP->datasetIdV);     // array only — strings are borrowed

  // qExpr, scopeExpr, geoRel were malloc'd by parsers — need recursive free
  // For now, accept the leak; these are small and the cache lives for the
  // broker's lifetime. A proper free would need ldQFree, ldScopeExprFree, etc.

  free(itemP);
}



// -----------------------------------------------------------------------------
//
// ldSubCacheItemRemove -
//
bool ldSubCacheItemRemove(LdSubCache* cacheP, const char* subId)
{
  if (cacheP == NULL || subId == NULL)
    return false;

  LdSubCacheItem* itemP = cacheP->itemList;
  LdSubCacheItem* prevP = NULL;

  while (itemP != NULL)
  {
    if (itemP->subId != NULL && strcmp(itemP->subId, subId) == 0)
    {
      if (prevP == NULL)
        cacheP->itemList = itemP->next;
      else
        prevP->next = itemP->next;

      if (itemP == cacheP->last)
        cacheP->last = prevP;

      cacheItemFree(itemP);
      return true;
    }

    prevP = itemP;
    itemP = itemP->next;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// ldSubCacheRelease -
//
void ldSubCacheRelease(LdSubCache* cacheP)
{
  if (cacheP == NULL)
    return;

  LdSubCacheItem* itemP = cacheP->itemList;

  while (itemP != NULL)
  {
    LdSubCacheItem* nextP = itemP->next;
    cacheItemFree(itemP);
    itemP = nextP;
  }

  free(cacheP);
}
