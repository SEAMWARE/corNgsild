//
// FILE            ldPernotCache.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <regex.h>                                     // regex_t, regcomp, regfree
#include <stdlib.h>                                    // calloc, malloc, free
#include <string.h>                                    // strcmp, strdup

#include "kalloc/KAlloc.h"                             // KAlloc, kaAlloc
#include "kalloc/kaBufferInit.h"                       // kaBufferInit
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjLookup.h"                            // kjLookup
#include "kjson/kjClone.h"                             // kjClone
#include "kjson/kjFree.h"                              // kjFree

#include "swNgsild/LdVocab.h"                          // LD_VOCAB_*
#include "swNgsild/LdPernotCache.h"                    // LdPernotCache, LdPernotItem
#include "swNgsild/ldPernotCache.h"                    // Own interface
#include "swNgsild/ldCheckDateTime.h"                  // ldIsoToNanoseconds



// -----------------------------------------------------------------------------
//
// stringArrayExtract - build NULL-terminated string array from a KjArray of strings
//
static char** stringArrayExtract(KjNode* arrayP)
{
  if (arrayP == NULL || arrayP->type != KjArray)
    return NULL;

  int count = 0;
  for (KjNode* p = arrayP->value.firstChildP; p != NULL; p = p->next)
    if (p->type == KjString) count++;
  if (count == 0)
    return NULL;

  char** v = (char**) malloc((count + 1) * sizeof(char*));
  int ix = 0;
  for (KjNode* p = arrayP->value.firstChildP; p != NULL; p = p->next)
    if (p->type == KjString)
      v[ix++] = p->value.s;
  v[ix] = NULL;
  return v;
}



// -----------------------------------------------------------------------------
//
// entitySelectorsExtractPernot - same as ldSubCache's but local
//
static LdSubEntitySelector* entitySelectorsExtractPernot(KjNode* entitiesP)
{
  if (entitiesP == NULL || entitiesP->type != KjArray)
    return NULL;

  LdSubEntitySelector* head = NULL;
  LdSubEntitySelector* tail = NULL;

  for (KjNode* entP = entitiesP->value.firstChildP; entP != NULL; entP = entP->next)
  {
    if (entP->type != KjObject) continue;

    KjNode* typeP      = kjLookup(entP, "type");
    KjNode* idP        = kjLookup(entP, "id");
    KjNode* idPatternP = kjLookup(entP, "idPattern");

    LdSubEntitySelector* esP = (LdSubEntitySelector*) calloc(1, sizeof(LdSubEntitySelector));
    esP->type = (typeP != NULL && typeP->type == KjString) ? typeP->value.s : NULL;
    esP->id   = (idP   != NULL && idP->type   == KjString) ? idP->value.s   : NULL;

    if (idPatternP != NULL && idPatternP->type == KjString)
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
LdPernotItem* ldPernotCacheItemAdd(LdPernotCache* cacheP, KjNode* subTree,
                                    LdQNode* qExpr, void* tenantP)
{
  if (cacheP == NULL || subTree == NULL)
    return NULL;

  LdPernotItem* itemP = (LdPernotItem*) calloc(1, sizeof(LdPernotItem));

  KjNode* idP = kjLookup(subTree, "id");
  itemP->subId   = (idP != NULL && idP->type == KjString) ? strdup(idP->value.s) : NULL;
  itemP->subTree = kjClone(NULL, subTree);

  // timeInterval
  KjNode* tiP = kjLookup(itemP->subTree, "timeInterval");
  itemP->timeInterval = (tiP != NULL && (tiP->type == KjInt || tiP->type == KjFloat))
                        ? (int) tiP->value.i : 0;

  // Entity selectors
  KjNode* entitiesP = kjLookup(itemP->subTree, LD_VOCAB_ENTITIES);
  itemP->entitySelectors = entitySelectorsExtractPernot(entitiesP);

  // Notification attrs + datasetId
  KjNode* notifP     = kjLookup(itemP->subTree, LD_VOCAB_NOTIFICATION);
  KjNode* notifAttrs = (notifP != NULL) ? kjLookup(notifP, LD_VOCAB_ATTRIBUTES) : NULL;
  itemP->notifAttrsV = stringArrayExtract(notifAttrs);

  KjNode* datasetIdP = kjLookup(itemP->subTree, LD_VOCAB_DATASET_ID);
  itemP->datasetIdV  = stringArrayExtract(datasetIdP);

  // q
  itemP->qExpr = qExpr;

  // scopeQ
  KjNode* scopeQP = kjLookup(itemP->subTree, "scopeQ");
  // For now, store raw — scopeQ parsing from cache is same as ldSubCache
  (void) scopeQP;

  // geoQ
  KjNode* geoQP = kjLookup(itemP->subTree, "geoQ");
  if (geoQP != NULL)
  {
    // TODO: parse geoQ fields into geoRel/geoGeometry/geoCoordinates/geoProperty
  }

  // Notification endpoint
  KjNode* endpointP = (notifP != NULL) ? kjLookup(notifP, LD_VOCAB_ENDPOINT) : NULL;
  KjNode* uriP      = (endpointP != NULL) ? kjLookup(endpointP, LD_VOCAB_URI) : NULL;
  itemP->endpointUri = (uriP != NULL && uriP->type == KjString) ? uriP->value.s : NULL;

  KjNode* formatP = (notifP != NULL) ? kjLookup(notifP, LD_VOCAB_FORMAT) : NULL;
  itemP->format = (formatP != NULL && formatP->type == KjString) ? formatP->value.s : NULL;

  KjNode* jcP = kjLookup(itemP->subTree, "jsonldContext");
  itemP->contextUrl = (jcP != NULL && jcP->type == KjString) ? jcP->value.s : NULL;

  // State
  KjNode* statusP = kjLookup(itemP->subTree, LD_VOCAB_STATUS);
  char* statusStr = (statusP != NULL && statusP->type == KjString) ? statusP->value.s : "active";
  if (strcmp(statusStr, "paused") == 0)
    itemP->state = LdPernotPaused;
  else
    itemP->state = LdPernotActive;

  // Expiration
  KjNode* expiresP = kjLookup(itemP->subTree, LD_VOCAB_EXPIRES_AT);
  if (expiresP != NULL && expiresP->type == KjString)
    itemP->expiresAt = ldIsoToNanoseconds(expiresP->value.s);

  itemP->tenantP = tenantP;

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
      if (p->subTree)     kjFree(p->subTree);
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
    if (p->subTree)     kjFree(p->subTree);
    entitySelectorsFree(p->entitySelectors);
    if (p->notifAttrsV) free(p->notifAttrsV);
    if (p->datasetIdV)  free(p->datasetIdV);
    free(p);
    p = next;
  }
  free(cacheP);
}
