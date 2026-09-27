//
// FILE            ldPernotCache.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <regex.h>                                     // regex_t, regcomp, regfree
#include <stdlib.h>                                    // calloc, malloc, free
#include <string.h>                                    // strcmp, strdup

#include "kalloc/KAlloc.h"                             // KAlloc, kaAlloc
#include "kalloc/kaBufferInit.h"                       // kaBufferInit
#include "corTree/CorNode.h"                           // CorNode
#include "corTree/corTreeLookup.h"                     // corTreeLookup
#include "corTree/corTreeClone.h"                      // corTreeClone
#include "corTree/corTreeBuilder.h"                    // corTreeChildRemove
#include "corTree/corTreeFree.h"                       // corTreeFree

#include "corNgsild/LdVocab.h"                          // LD_VOCAB_*
#include "corNgsild/LdPernotCache.h"                    // LdPernotCache, LdPernotItem
#include "corNgsild/ldPernotCache.h"                    // Own interface
#include "corNgsild/ldCheckDateTime.h"                  // ldIsoToNanoseconds



// -----------------------------------------------------------------------------
//
// stringArrayExtract - build NULL-terminated string array from a CorArray of strings
//
static char** stringArrayExtract(CorNode* arrayP)
{
  if (arrayP == NULL || arrayP->type != CorArray)
    return NULL;

  int count = 0;
  for (CorNode* p = arrayP->value.head; p != NULL; p = p->next)
    if (p->type == CorString) count++;
  if (count == 0)
    return NULL;

  char** v = (char**) malloc((count + 1) * sizeof(char*));
  int ix = 0;
  for (CorNode* p = arrayP->value.head; p != NULL; p = p->next)
    if (p->type == CorString)
      v[ix++] = p->value.s;
  v[ix] = NULL;
  return v;
}



// -----------------------------------------------------------------------------
//
// entitySelectorsExtractPernot - same as ldSubCache's but local
//
static LdSubEntitySelector* entitySelectorsExtractPernot(CorNode* entitiesP)
{
  if (entitiesP == NULL || entitiesP->type != CorArray)
    return NULL;

  LdSubEntitySelector* head = NULL;
  LdSubEntitySelector* tail = NULL;

  for (CorNode* entP = entitiesP->value.head; entP != NULL; entP = entP->next)
  {
    if (entP->type != CorObject) continue;

    CorNode* typeP     = corTreeLookup(entP, "type");
    CorNode* idP       = corTreeLookup(entP, "id");
    CorNode* idPatternP = corTreeLookup(entP, "idPattern");

    LdSubEntitySelector* esP = (LdSubEntitySelector*) calloc(1, sizeof(LdSubEntitySelector));
    esP->type = (typeP != NULL && typeP->type == CorString) ? typeP->value.s : NULL;
    esP->id   = (idP   != NULL && idP->type   == CorString) ? idP->value.s  : NULL;

    if (idPatternP != NULL && idPatternP->type == CorString)
    {
      LdSubIdPattern* ripP = (LdSubIdPattern*) calloc(1, sizeof(LdSubIdPattern));
      if (regcomp(&ripP->regex, idPatternP->value.s, REG_EXTENDED | REG_NOSUB) == 0)
        esP->idPatternList = ripP;
      else
        free(ripP);
    }

    if (tail == NULL) head = esP;
    else              tail->next = esP;
    tail = esP;
  }
  return head;
}



// -----------------------------------------------------------------------------
//
// entitySelectorsFree -
//
static void entitySelectorsFree(LdSubEntitySelector* head)
{
  while (head != NULL)
  {
    LdSubEntitySelector* next = head->next;
    for (LdSubIdPattern* ripP = head->idPatternList; ripP != NULL; )
    {
      LdSubIdPattern* nextR = ripP->next;
      regfree(&ripP->regex);
      free(ripP);
      ripP = nextR;
    }
    free(head);
    head = next;
  }
}



// ldPernotCacheCreate
LdPernotCache* ldPernotCacheCreate(void)
{
  LdPernotCache* cacheP = (LdPernotCache*) calloc(1, sizeof(LdPernotCache));
  kaBufferInit(&cacheP->alloc, cacheP->allocBuf, sizeof(cacheP->allocBuf), 4096, NULL, "pernot-cache");
  return cacheP;
}



// ldPernotCacheItemAdd
LdPernotItem* ldPernotCacheItemAdd(LdPernotCache* cacheP, CorNode* subTree,
                                    LdQNode* qExpr, void* tenantP)
{
  if (cacheP == NULL || subTree == NULL)
    return NULL;

  LdPernotItem* itemP = (LdPernotItem*) calloc(1, sizeof(LdPernotItem));

  CorNode* idP = corTreeLookup(subTree, "id");
  itemP->subId   = (idP != NULL && idP->type == CorString) ? strdup(idP->value.s) : NULL;
  itemP->subTree = corTreeClone(NULL, subTree);

  // timeInterval
  CorNode* tiP = corTreeLookup(itemP->subTree, "timeInterval");
  itemP->timeInterval = (tiP != NULL && (tiP->type == CorInt || tiP->type == CorFloat))
                        ? (int) tiP->value.i : 0;

  // Entity selectors
  CorNode* entitiesP = corTreeLookup(itemP->subTree, LD_VOCAB_ENTITIES);
  itemP->entitySelectors = entitySelectorsExtractPernot(entitiesP);

  // Notification attrs + datasetId
  CorNode* notifP    = corTreeLookup(itemP->subTree, LD_VOCAB_NOTIFICATION);
  CorNode* notifAttrs = (notifP != NULL) ? corTreeLookup(notifP, LD_VOCAB_ATTRIBUTES) : NULL;
  itemP->notifAttrsV = stringArrayExtract(notifAttrs);

  CorNode* datasetIdP = corTreeLookup(itemP->subTree, LD_VOCAB_DATASET_ID);
  itemP->datasetIdV  = stringArrayExtract(datasetIdP);

  // q
  itemP->qExpr = qExpr;

  // scopeQ
  CorNode* scopeQP = corTreeLookup(itemP->subTree, "scopeQ");
  // For now, store raw — scopeQ parsing from cache is same as ldSubCache
  (void) scopeQP;

  // geoQ
  CorNode* geoQP = corTreeLookup(itemP->subTree, "geoQ");
  if (geoQP != NULL)
  {
    // TODO: parse geoQ fields into geoRel/geoGeometry/geoCoordinates/geoProperty
  }

  // Notification endpoint
  CorNode* endpointP = (notifP != NULL) ? corTreeLookup(notifP, LD_VOCAB_ENDPOINT) : NULL;
  CorNode* uriP     = (endpointP != NULL) ? corTreeLookup(endpointP, LD_VOCAB_URI) : NULL;
  itemP->endpointUri = (uriP != NULL && uriP->type == CorString) ? uriP->value.s : NULL;

  // § 5.2.15 endpoint.cooldown — minimum ms before retrying after failure.
  CorNode* coolP = (endpointP != NULL) ? corTreeLookup(endpointP, "cooldown") : NULL;
  if (coolP != NULL && (coolP->type == CorInt || coolP->type == CorFloat))
  {
    double ms = (coolP->type == CorInt) ? (double) coolP->value.i : coolP->value.f;
    if (ms > 0)
      itemP->cooldownNs = (uint64_t) (ms * 1000000.0);
  }

  // § 5.2.15 endpoint.timeout — max ms to wait for a notification reply.
  CorNode* tmoP = (endpointP != NULL) ? corTreeLookup(endpointP, "timeout") : NULL;
  if (tmoP != NULL && (tmoP->type == CorInt || tmoP->type == CorFloat))
  {
    double ms = (tmoP->type == CorInt) ? (double) tmoP->value.i : tmoP->value.f;
    if (ms > 0)
      itemP->timeoutMs = (int) ms;
  }

  // § 5.2.15 endpoint.receiverInfo — KeyValuePair[] forwarded as outbound headers.
  CorNode* riP = (endpointP != NULL) ? corTreeLookup(endpointP, "receiverInfo") : NULL;
  if (riP != NULL && riP->type == CorArray)
    itemP->receiverInfo = riP;

  // § 5.2.14 notification.join + notification.joinLevel — linked-entity retrieval (§ 4.5.23)
  CorNode* joinP     = (notifP != NULL) ? corTreeLookup(notifP, "join") : NULL;
  CorNode* joinLevelP = (notifP != NULL) ? corTreeLookup(notifP, "joinLevel") : NULL;
  if (joinP != NULL && joinP->type == CorString)
    itemP->notifJoin = joinP->value.s;
  if (joinLevelP != NULL && joinLevelP->type == CorInt && joinLevelP->value.i > 0)
    itemP->notifJoinLevel = (int) joinLevelP->value.i;

  CorNode* formatP = (notifP != NULL) ? corTreeLookup(notifP, LD_VOCAB_FORMAT) : NULL;
  itemP->format = (formatP != NULL && formatP->type == CorString) ? formatP->value.s : NULL;

  // User-provided `jsonldContext` wins; otherwise fall back to broker-filled
  // `_jcResolved`. Same convention as the regular sub cache.
  CorNode* jcP = corTreeLookup(itemP->subTree, "jsonldContext");
  if (jcP == NULL || jcP->type != CorString)
    jcP = corTreeLookup(itemP->subTree, "_jcResolved");
  itemP->contextUrl = (jcP != NULL && jcP->type == CorString) ? jcP->value.s : NULL;

  // State
  CorNode* statusP = corTreeLookup(itemP->subTree, LD_VOCAB_STATUS);
  char* statusStr = (statusP != NULL && statusP->type == CorString) ? statusP->value.s : "active";
  if (strcmp(statusStr, "paused") == 0)
    itemP->state = LdPernotPaused;
  else
    itemP->state = LdPernotActive;

  // Expiration
  CorNode* expiresP = corTreeLookup(itemP->subTree, LD_VOCAB_EXPIRES_AT);
  if (expiresP != NULL && expiresP->type == CorString)
    itemP->expiresAt = ldIsoToNanoseconds(expiresP->value.s);

  itemP->tenantP = tenantP;

  //
  // Stats fields — present when this item came from a mongo-load (a prior
  // flush persisted them). Extract into cache fields and remove from the
  // stored subTree so the GET-response injector owns the final values.
  //
  if (notifP != NULL && notifP->type == CorObject)
  {
    CorNode* tsP = corTreeLookup(notifP, "timesSent");
    CorNode* tfP = corTreeLookup(notifP, "timesFailed");
    CorNode* lnP = corTreeLookup(notifP, "lastNotification");
    CorNode* lsP = corTreeLookup(notifP, "lastSuccess");
    CorNode* lfP = corTreeLookup(notifP, "lastFailure");

    if (tsP != NULL && tsP->type == CorInt) itemP->timesSent  = (int) tsP->value.i;
    if (tfP != NULL && tfP->type == CorInt) itemP->timesFailed = (int) tfP->value.i;
    if (lnP != NULL)
    {
      if      (lnP->type == CorInt)   itemP->lastNotification = (uint64_t) lnP->value.i;
      else if (lnP->type == CorString) itemP->lastNotification = ldIsoToNanoseconds(lnP->value.s);
    }
    if (lsP != NULL)
    {
      if      (lsP->type == CorInt)   itemP->lastSuccess = (uint64_t) lsP->value.i;
      else if (lsP->type == CorString) itemP->lastSuccess = ldIsoToNanoseconds(lsP->value.s);
    }
    if (lfP != NULL)
    {
      if      (lfP->type == CorInt)   itemP->lastFailure = (uint64_t) lfP->value.i;
      else if (lfP->type == CorString) itemP->lastFailure = ldIsoToNanoseconds(lfP->value.s);
    }

    itemP->lastFlushedSent   = itemP->timesSent;
    itemP->lastFlushedFailed = itemP->timesFailed;

    // corTreeChildRemove only unlinks; subTree is a malloc clone, so free each
    // stripped stat node or it leaks on reload of a flushed (persisted-stats) sub.
    if (tsP != NULL) { corTreeChildRemove(notifP, tsP); corTreeFree(tsP); }
    if (tfP != NULL) { corTreeChildRemove(notifP, tfP); corTreeFree(tfP); }
    if (lnP != NULL) { corTreeChildRemove(notifP, lnP); corTreeFree(lnP); }
    if (lsP != NULL) { corTreeChildRemove(notifP, lsP); corTreeFree(lsP); }
    if (lfP != NULL) { corTreeChildRemove(notifP, lfP); corTreeFree(lfP); }
  }

  // Append
  if (cacheP->tail == NULL)
    cacheP->head = itemP;
  else
    cacheP->tail->next = itemP;
  cacheP->tail = itemP;

  return itemP;
}



// ldPernotCacheItemLookup
LdPernotItem* ldPernotCacheItemLookup(LdPernotCache* cacheP, const char* subId)
{
  if (cacheP == NULL || subId == NULL) return NULL;
  for (LdPernotItem* p = cacheP->head; p != NULL; p = p->next)
    if (p->subId != NULL && strcmp(p->subId, subId) == 0)
      return p;
  return NULL;
}



// ldPernotCacheItemRemove
bool ldPernotCacheItemRemove(LdPernotCache* cacheP, const char* subId)
{
  if (cacheP == NULL || subId == NULL) return false;

  LdPernotItem* prev = NULL;
  for (LdPernotItem* p = cacheP->head; p != NULL; p = p->next)
  {
    if (p->subId != NULL && strcmp(p->subId, subId) == 0)
    {
      if (prev == NULL) cacheP->head = p->next;
      else              prev->next   = p->next;
      if (cacheP->tail == p) cacheP->tail = prev;

      free(p->subId);
      if (p->subTree)     corTreeFree(p->subTree);
      entitySelectorsFree(p->entitySelectors);
      if (p->notifAttrsV) free(p->notifAttrsV);
      if (p->datasetIdV)  free(p->datasetIdV);
      free(p);
      return true;
    }
    prev = p;
  }
  return false;
}



// ldPernotCacheRelease
void ldPernotCacheRelease(LdPernotCache* cacheP)
{
  if (cacheP == NULL) return;
  LdPernotItem* p = cacheP->head;
  while (p != NULL)
  {
    LdPernotItem* next = p->next;
    free(p->subId);
    if (p->subTree)     corTreeFree(p->subTree);
    entitySelectorsFree(p->entitySelectors);
    if (p->notifAttrsV) free(p->notifAttrsV);
    if (p->datasetIdV)  free(p->datasetIdV);
    free(p);
    p = next;
  }
  free(cacheP);
}
