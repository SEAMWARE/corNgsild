//
// FILE            ldRegCache.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Context Source Registration cache operations (NGSI-LD § 5.9 / § 5.10).
//
#include <regex.h>                                     // regcomp, regfree
#include <stdlib.h>                                    // calloc, free, strdup
#include <string.h>                                    // strcmp

#include "kalloc/kaBufferInit.h"                       // kaBufferInit
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjClone.h"                             // kjClone
#include "kjson/kjFree.h"                              // kjFree
#include "kjson/kjLookup.h"                            // kjLookup

#include "kalloc/KAlloc.h"                              // KAlloc
#include "swJsonld/swldExpand.h"                       // swldExpand, swldAlreadyExpanded
#include "swNgsild/LdVocab.h"                          // LD_VOCAB_*
#include "swNgsild/LdRegCache.h"                       // LdRegCache, LdRegCacheItem
#include "swNgsild/ldCheckDateTime.h"                  // ldIsoToNanoseconds
#include "swNgsild/ldRegCache.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// idPatternCompile - compile an idPattern regex; returns NULL on failure
//
static LdRegIdPattern* idPatternCompile(const char* pattern)
{
  LdRegIdPattern* ripP = (LdRegIdPattern*) calloc(1, sizeof(LdRegIdPattern));

  if (regcomp(&ripP->regex, pattern, REG_EXTENDED | REG_NOSUB) != 0)
  {
    free(ripP);
    return NULL;
  }

  return ripP;
}



// -----------------------------------------------------------------------------
//
// entityInfoExtract - flatten one entities[] entry into LdRegEntityInfo nodes
//
// `type` may be a string or a string array (multi-type). Each value
// becomes a separate LdRegEntityInfo entry sharing the entry's id /
// idPattern (matching is OR over all entries).
//
static LdRegEntityInfo* entityInfoExtract(KjNode* entP)
{
  if (entP == NULL || entP->type != KjObject)
    return NULL;

  KjNode* typeP    = kjLookup(entP, "type");
  KjNode* idP      = kjLookup(entP, "id");
  KjNode* idPatP   = kjLookup(entP, LD_VOCAB_ID_PATTERN);

  if (typeP == NULL)
    return NULL;

  char* idVal       = (idP    != NULL && idP->type    == KjString) ? idP->value.s    : NULL;
  char* idPatternS  = (idPatP != NULL && idPatP->type == KjString) ? idPatP->value.s : NULL;

  LdRegEntityInfo* head = NULL;
  LdRegEntityInfo* tail = NULL;

  // Walk type values (one for KjString, N for KjArray of strings)
  KjNode* tValP = (typeP->type == KjArray) ? typeP->value.firstChildP : typeP;

  for (; tValP != NULL; tValP = (typeP->type == KjArray) ? tValP->next : NULL)
  {
    if (tValP->type != KjString)
      continue;

    LdRegEntityInfo* eiP = (LdRegEntityInfo*) calloc(1, sizeof(LdRegEntityInfo));
    eiP->type = tValP->value.s;   // borrowed pointer into cloned regTree
    eiP->id   = idVal;            // borrowed

    if (idPatternS != NULL)
      eiP->idPatternList = idPatternCompile(idPatternS);

    if (tail == NULL)
      head = eiP;
    else
      tail->next = eiP;
    tail = eiP;
  }

  return head;
}



// -----------------------------------------------------------------------------
//
// stringArrayExtract - build NULL-terminated string array from a KjArray
//
// Returns NULL if input is absent or empty. Strings are borrowed pointers
// into the cloned regTree. Use this for verbatim string lists (operations,
// etc.). For attribute-name lists (propertyNames / relationshipNames) use
// attrIRIArrayExtract which also vocab-expands each entry.
//
static char** stringArrayExtract(KjNode* arrP)
{
  if (arrP == NULL || arrP->type != KjArray)
    return NULL;

  int count = 0;
  for (KjNode* sP = arrP->value.firstChildP; sP != NULL; sP = sP->next)
    if (sP->type == KjString)
      count++;

  if (count == 0)
    return NULL;

  char** v = (char**) malloc((count + 1) * sizeof(char*));
  int    ix = 0;

  for (KjNode* sP = arrP->value.firstChildP; sP != NULL; sP = sP->next)
  {
    if (sP->type == KjString)
      v[ix++] = sP->value.s;
  }

  v[ix] = NULL;
  return v;
}



// -----------------------------------------------------------------------------
//
// attrIRIArrayExtract - like stringArrayExtract, but vocab-expands each entry
//
// swldExpandTree intentionally does NOT perform @type:@vocab/@type:@id value
// coercion (that would silently launder sketchy user input past validators
// that run post-expansion — see swldExpandTree.c's comment). So values
// inside arrays like propertyNames / relationshipNames stay short after
// parseHook. Downstream matching against entity attribute names (which
// ARE stored fully-expanded) needs the IRI form, so we expand here at
// cache-ingest time, once. Mirrors ldSubCache.c's notifAttrsV expansion.
//
static char** attrIRIArrayExtract(KjNode* arrP, KAlloc* allocP)
{
  char** v = stringArrayExtract(arrP);
  if (v == NULL)
    return NULL;

  for (int i = 0; v[i] != NULL; i++)
  {
    if (swldAlreadyExpanded(v[i]) == false)
    {
      char* expanded = swldExpand(NULL, v[i], allocP, NULL, NULL);
      if (expanded != NULL)
        v[i] = expanded;
    }
  }
  return v;
}



// -----------------------------------------------------------------------------
//
// infoListExtract - parse the information[] array into a linked list
//
static LdRegInfo* infoListExtract(KjNode* infoArrayP, KAlloc* allocP)
{
  if (infoArrayP == NULL || infoArrayP->type != KjArray)
    return NULL;

  LdRegInfo* head = NULL;
  LdRegInfo* tail = NULL;

  for (KjNode* infoP = infoArrayP->value.firstChildP; infoP != NULL; infoP = infoP->next)
  {
    if (infoP->type != KjObject)
      continue;

    LdRegInfo* riP = (LdRegInfo*) calloc(1, sizeof(LdRegInfo));

    KjNode* entitiesP   = kjLookup(infoP, LD_VOCAB_ENTITIES);
    KjNode* propsP      = kjLookup(infoP, "propertyNames");
    KjNode* relsP       = kjLookup(infoP, "relationshipNames");

    if (entitiesP != NULL && entitiesP->type == KjArray)
    {
      LdRegEntityInfo* eHead = NULL;
      LdRegEntityInfo* eTail = NULL;

      for (KjNode* entP = entitiesP->value.firstChildP; entP != NULL; entP = entP->next)
      {
        LdRegEntityInfo* sub = entityInfoExtract(entP);
        if (sub == NULL)
          continue;

        if (eTail == NULL)
          eHead = sub;
        else
          eTail->next = sub;
        // advance eTail to the new sublist's tail
        for (eTail = sub; eTail->next != NULL; eTail = eTail->next) { }
      }

      riP->entityInfoV = eHead;
    }

    riP->propertyNamesV     = attrIRIArrayExtract(propsP, allocP);
    riP->relationshipNamesV = attrIRIArrayExtract(relsP,  allocP);

    if (tail == NULL)
      head = riP;
    else
      tail->next = riP;
    tail = riP;
  }

  return head;
}



// -----------------------------------------------------------------------------
//
// modeFromString - map "inclusive" / "exclusive" / "redirect" / "auxiliary"
//
static LdRegMode modeFromString(const char* s)
{
  if (s == NULL)                          return LdRegModeInclusive;
  if (strcmp(s, "exclusive") == 0)        return LdRegModeExclusive;
  if (strcmp(s, "redirect")  == 0)        return LdRegModeRedirect;
  if (strcmp(s, "auxiliary") == 0)        return LdRegModeAuxiliary;
  return LdRegModeInclusive;
}



// -----------------------------------------------------------------------------
//
// entityInfoListFree - free a linked list of LdRegEntityInfo (with regexes)
//
static void entityInfoListFree(LdRegEntityInfo* head)
{
  while (head != NULL)
  {
    LdRegEntityInfo* next = head->next;

    LdRegIdPattern* ripP = head->idPatternList;
    while (ripP != NULL)
    {
      LdRegIdPattern* ripNext = ripP->next;
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
// infoListFree - free a linked list of LdRegInfo
//
static void infoListFree(LdRegInfo* head)
{
  while (head != NULL)
  {
    LdRegInfo* next = head->next;

    entityInfoListFree(head->entityInfoV);

    if (head->propertyNamesV     != NULL)  free(head->propertyNamesV);
    if (head->relationshipNamesV != NULL)  free(head->relationshipNamesV);

    free(head);
    head = next;
  }
}



// -----------------------------------------------------------------------------
//
// ldRegCacheCreate -
//
LdRegCache* ldRegCacheCreate(void)
{
  LdRegCache* cacheP = (LdRegCache*) calloc(1, sizeof(LdRegCache));

  kaBufferInit(&cacheP->alloc, cacheP->allocBuf, sizeof(cacheP->allocBuf), 4096, NULL, "regCache");

  return cacheP;
}



// -----------------------------------------------------------------------------
//
// ldRegCacheItemAdd -
//
LdRegCacheItem* ldRegCacheItemAdd(LdRegCache* cacheP, KjNode* regTree)
{
  if (cacheP == NULL || regTree == NULL)
    return NULL;

  LdRegCacheItem* itemP = (LdRegCacheItem*) calloc(1, sizeof(LdRegCacheItem));

  // Clone the registration tree (malloc allocator — persists across requests)
  itemP->regTree = kjClone(NULL, regTree);

  // Extract registration ID
  KjNode* idP = kjLookup(itemP->regTree, "id");
  itemP->regId = (idP != NULL && idP->type == KjString) ? strdup(idP->value.s) : NULL;

  // Pre-parse RegistrationInfo[] for matching
  KjNode* infoP = kjLookup(itemP->regTree, LD_VOCAB_INFORMATION);
  itemP->infoV = infoListExtract(infoP, &cacheP->alloc);

  // mode (default inclusive)
  KjNode* modeP = kjLookup(itemP->regTree, LD_VOCAB_MODE);
  itemP->mode = (modeP != NULL && modeP->type == KjString) ? modeFromString(modeP->value.s) : LdRegModeInclusive;

  // operations (NULL = federationOps default)
  KjNode* opsP = kjLookup(itemP->regTree, "operations");
  itemP->operationsV = stringArrayExtract(opsP);

  // endpoint (borrowed)
  KjNode* endpointP = kjLookup(itemP->regTree, LD_VOCAB_ENDPOINT);
  itemP->endpoint = (endpointP != NULL && endpointP->type == KjString) ? endpointP->value.s : NULL;

  // contextSourceAlias (borrowed; NGSI-LD § 5.2.9 — distribution loop detection)
  KjNode* aliasP = kjLookup(itemP->regTree, "contextSourceAlias");
  itemP->csourceAlias = (aliasP != NULL && aliasP->type == KjString) ? aliasP->value.s : NULL;

  // expiresAt
  KjNode* expiresP = kjLookup(itemP->regTree, LD_VOCAB_EXPIRES_AT);
  if (expiresP != NULL && expiresP->type == KjString)
    itemP->expiresAt = ldIsoToNanoseconds(expiresP->value.s);

  // Append to cache linked list
  if (cacheP->last == NULL)
    cacheP->itemList = itemP;
  else
    cacheP->last->next = itemP;
  cacheP->last = itemP;

  return itemP;
}



// -----------------------------------------------------------------------------
//
// ldRegCacheItemLookup -
//
LdRegCacheItem* ldRegCacheItemLookup(LdRegCache* cacheP, const char* regId)
{
  if (cacheP == NULL || regId == NULL)
    return NULL;

  for (LdRegCacheItem* itemP = cacheP->itemList; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->regId != NULL && strcmp(itemP->regId, regId) == 0)
      return itemP;
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// cacheItemFree - free a single cache item and all its resources
//
static void cacheItemFree(LdRegCacheItem* itemP)
{
  if (itemP->regId != NULL)
    free(itemP->regId);

  if (itemP->regTree != NULL)
    kjFree(itemP->regTree);

  infoListFree(itemP->infoV);

  if (itemP->operationsV != NULL)
    free(itemP->operationsV);  // array only — strings are borrowed

  free(itemP);
}



// -----------------------------------------------------------------------------
//
// ldRegCacheItemRemove -
//
bool ldRegCacheItemRemove(LdRegCache* cacheP, const char* regId)
{
  if (cacheP == NULL || regId == NULL)
    return false;

  LdRegCacheItem* itemP = cacheP->itemList;
  LdRegCacheItem* prevP = NULL;

  while (itemP != NULL)
  {
    if (itemP->regId != NULL && strcmp(itemP->regId, regId) == 0)
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
// entityInfoMatches - apply the § 5.12 EntityInfo match against an id+type
//
// Returns true if this EntityInfo entry matches the requested entity.
// type matching: NULL entityType (caller doesn't know) bypasses type
// constraint and accepts any.
//
static bool entityInfoMatches(LdRegEntityInfo* eiP, const char* entityId, char** entityTypeV)
{
  if (entityTypeV != NULL && eiP->type != NULL)
  {
    bool typeMatch = false;
    for (int i = 0; entityTypeV[i] != NULL; i++)
      if (strcmp(eiP->type, entityTypeV[i]) == 0) { typeMatch = true; break; }
    if (!typeMatch)
      return false;
  }

  if (eiP->id == NULL && eiP->idPatternList == NULL)
    return true;

  if (eiP->id != NULL && strcmp(eiP->id, entityId) == 0)
    return true;

  for (LdRegIdPattern* ripP = eiP->idPatternList; ripP != NULL; ripP = ripP->next)
  {
    if (regexec(&ripP->regex, entityId, 0, NULL, 0) == 0)
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// itemMatches - any RegistrationInfo / EntityInfo entry matches?
//
static bool itemMatches(LdRegCacheItem* itemP, const char* entityId, char** entityTypeV)
{
  for (LdRegInfo* riP = itemP->infoV; riP != NULL; riP = riP->next)
  {
    for (LdRegEntityInfo* eiP = riP->entityInfoV; eiP != NULL; eiP = eiP->next)
    {
      if (entityInfoMatches(eiP, entityId, entityTypeV))
        return true;
    }
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// ldRegCacheMatchForRetrieve -
//
int ldRegCacheMatchForRetrieve(LdRegCache*       cacheP,
                                const char*       entityId,
                                char**            entityTypeV,
                                LdRegMode         modeFilter,
                                LdRegCacheItem*** matchVP)
{
  if (cacheP == NULL || entityId == NULL || matchVP == NULL)
    return 0;

  // Two-pass: count first, then allocate exactly + populate
  int count = 0;
  for (LdRegCacheItem* itemP = cacheP->itemList; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->mode != modeFilter)
      continue;
    if (itemMatches(itemP, entityId, entityTypeV))
      count++;
  }

  if (count == 0)
    return 0;

  LdRegCacheItem** v = (LdRegCacheItem**) malloc(count * sizeof(LdRegCacheItem*));
  int ix = 0;

  for (LdRegCacheItem* itemP = cacheP->itemList; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->mode != modeFilter)
      continue;
    if (itemMatches(itemP, entityId, entityTypeV))
      v[ix++] = itemP;
  }

  *matchVP = v;
  return count;
}



// -----------------------------------------------------------------------------
//
// ldRegCacheRelease -
//
void ldRegCacheRelease(LdRegCache* cacheP)
{
  if (cacheP == NULL)
    return;

  LdRegCacheItem* itemP = cacheP->itemList;

  while (itemP != NULL)
  {
    LdRegCacheItem* nextP = itemP->next;
    cacheItemFree(itemP);
    itemP = nextP;
  }

  free(cacheP);
}
