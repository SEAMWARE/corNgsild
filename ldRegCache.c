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
#include <time.h>                                      // clock_gettime

#include "kalloc/kaBufferInit.h"                       // kaBufferInit
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjClone.h"                             // kjClone
#include "kjson/kjFree.h"                              // kjFree
#include "kjson/kjLookup.h"                            // kjLookup

#include "kalloc/KAlloc.h"                              // KAlloc
#include "swJsonld/swldExpand.h"                       // swldExpand, swldAlreadyExpanded
#include "swJsonld/swldDownload.h"                     // swldContextFromUrl
#include "swJsonld/swldInit.h"                         // swldCoreContext
#include "swNgsild/LdVocab.h"                          // LD_VOCAB_*
#include "swNgsild/LdRegCache.h"                       // LdRegCache, LdRegCacheItem
#include "swNgsild/ldCheckDateTime.h"                  // ldIsoToNanoseconds
#include "swNgsild/ldScopeMatch.h"                     // ldScopePatternMatch
#include "swNgsild/ldProbeSourceIdentity.h"            // ldProbeSourceIdentity
#include "swNgsild/ldRegCache.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// csrScopeMatches - does CSR's scopeV cover any entry in entityScopeV?
//
// Returns true if:
//   - the CSR has no scope filter (scopeV == NULL), OR
//   - entityScopeV has at least one value that matches at least one
//     pattern in csrScopeV via ldScopePatternMatch.
//
// If the CSR declares a scope but the entity has no scope, the match
// fails (scope-restricted CSRs need a scoped entity to apply to).
//
static bool csrScopeMatches(char** csrScopeV, char** entityScopeV)
{
  if (csrScopeV == NULL)
    return true;

  if (entityScopeV == NULL || entityScopeV[0] == NULL)
    return false;

  for (int i = 0; entityScopeV[i] != NULL; i++)
    for (int j = 0; csrScopeV[j] != NULL; j++)
      if (ldScopePatternMatch(csrScopeV[j], entityScopeV[i]))
        return true;

  return false;
}



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

  ripP->source = pattern;   // borrowed from regTree (lives as long as the cache item)
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

  // operations — OR'd LdOp bits. § 4.20: when operations[] is absent,
  // the default group "federationOps" applies. Resolve that once here
  // so the match path is a single AND with no default fallback logic.
  KjNode* opsP = kjLookup(itemP->regTree, "operations");
  if (opsP != NULL && opsP->type == KjArray && opsP->value.firstChildP != NULL)
  {
    itemP->operationsMask = 0;
    for (KjNode* e = opsP->value.firstChildP; e != NULL; e = e->next)
    {
      if (e->type != KjString)
        continue;

      LdOp bit = ldOpFromName(e->value.s);
      if (bit != LdOpNone)
        itemP->operationsMask |= (uint64_t) bit;
      else
        itemP->operationsMask |= ldOpGroupMaskFromName(e->value.s);
    }
  }
  else
  {
    itemP->operationsMask = LD_OP_GROUP_FEDERATION;
  }

  // endpoint (borrowed)
  KjNode* endpointP = kjLookup(itemP->regTree, LD_VOCAB_ENDPOINT);
  itemP->endpoint = (endpointP != NULL && endpointP->type == KjString) ? endpointP->value.s : NULL;

  // tenant (borrowed; NGSI-LD § 5.2.9 — forwarded requests carry
  // NGSILD-Tenant: <tenant>, letting a single broker instance back itself
  // under a different tenancy without a Via-loop false positive)
  KjNode* tenantP = kjLookup(itemP->regTree, "tenant");
  itemP->tenant = (tenantP != NULL && tenantP->type == KjString) ? tenantP->value.s : NULL;

  // contextSourceAlias (borrowed; NGSI-LD § 5.2.9 — distribution loop
  // detection). If the client didn't provide one, probe the CSR's
  // /info/sourceIdentity endpoint (§ 5.15) to learn it. The probe's
  // result is process-heap malloc'd; probedAlias records ownership so
  // the item-free path can free it. Probe failure leaves csourceAlias
  // NULL — reactive Via-based detection still protects us.
  KjNode* aliasP = kjLookup(itemP->regTree, "contextSourceAlias");
  if (aliasP != NULL && aliasP->type == KjString)
  {
    itemP->csourceAlias = aliasP->value.s;
    itemP->probedAlias  = NULL;
  }
  else if (itemP->endpoint != NULL)
  {
    itemP->probedAlias  = ldProbeSourceIdentity(itemP->endpoint, itemP->tenant, 0);
    itemP->csourceAlias = itemP->probedAlias;   // may be NULL on failure
  }

  // Geo coverage borrowed pointers — § 5.2.9. Match-time filtering
  // requires GEOS integration into swNgsild; tracked as a gap.
  itemP->locationP         = kjLookup(itemP->regTree, "location");
  itemP->observationSpaceP = kjLookup(itemP->regTree, "observationSpace");
  itemP->operationSpaceP   = kjLookup(itemP->regTree, "operationSpace");

  // management.timeout (§ 5.2.34) — per-CSR request timeout in ms
  KjNode* mgmtP = kjLookup(itemP->regTree, "management");
  if (mgmtP != NULL && mgmtP->type == KjObject)
  {
    KjNode* toP = kjLookup(mgmtP, "timeout");
    if (toP != NULL)
    {
      if (toP->type == KjInt)    itemP->timeoutMs = (int) toP->value.i;
      else if (toP->type == KjFloat) itemP->timeoutMs = (int) toP->value.f;
    }

    // management.cooldown (§ 5.2.34) — after a forward failure, decline
    // to contact the endpoint until this many ms have elapsed
    KjNode* cdP = kjLookup(mgmtP, "cooldown");
    if (cdP != NULL)
    {
      if (cdP->type == KjInt)        itemP->cooldownMs = (int) cdP->value.i;
      else if (cdP->type == KjFloat) itemP->cooldownMs = (int) cdP->value.f;
    }
  }

  // scope (§ 5.2.9) — scope or scope[] the CSR claims. Normalize both
  // KjString and KjArray cases into a NULL-terminated char* array.
  KjNode* scopeP = kjLookup(itemP->regTree, LD_VOCAB_SCOPE);
  if (scopeP != NULL)
  {
    if (scopeP->type == KjString)
    {
      itemP->scopeV = (char**) malloc(2 * sizeof(char*));
      itemP->scopeV[0] = scopeP->value.s;
      itemP->scopeV[1] = NULL;
    }
    else if (scopeP->type == KjArray)
    {
      int n = 0;
      for (KjNode* sP = scopeP->value.firstChildP; sP != NULL; sP = sP->next)
        if (sP->type == KjString) n++;
      if (n > 0)
      {
        itemP->scopeV = (char**) malloc((n + 1) * sizeof(char*));
        int ix = 0;
        for (KjNode* sP = scopeP->value.firstChildP; sP != NULL; sP = sP->next)
          if (sP->type == KjString)
            itemP->scopeV[ix++] = sP->value.s;
        itemP->scopeV[ix] = NULL;
      }
    }
  }

  // contextSourceInfo (§ 5.2.22) — arbitrary KV pairs forwarded as HTTP
  // headers. Flatten [{key,value}, ...] → [k0, v0, k1, v1, ..., NULL].
  KjNode* csiP = kjLookup(itemP->regTree, "contextSourceInfo");
  if (csiP != NULL && csiP->type == KjArray)
  {
    int n = 0;
    for (KjNode* kvP = csiP->value.firstChildP; kvP != NULL; kvP = kvP->next)
      if (kvP->type == KjObject)
        n++;

    if (n > 0)
    {
      itemP->contextSourceInfoKV = (char**) malloc((n * 2 + 1) * sizeof(char*));
      int ix = 0;
      for (KjNode* kvP = csiP->value.firstChildP; kvP != NULL; kvP = kvP->next)
      {
        if (kvP->type != KjObject) continue;
        KjNode* keyP = kjLookup(kvP, "key");
        KjNode* valP = kjLookup(kvP, "value");
        if (keyP == NULL || keyP->type != KjString) continue;
        if (valP == NULL || valP->type != KjString) continue;

        // Borrow from regTree (cache owns regTree lifetime)
        itemP->contextSourceInfoKV[ix++] = keyP->value.s;
        itemP->contextSourceInfoKV[ix++] = valP->value.s;
      }
      itemP->contextSourceInfoKV[ix] = NULL;
    }
  }

  // Forwarding @context: pre-resolve once at registration time so every
  // distop forward to this CSR can compact alias-bearing values (type,
  // pick attrs, ...) into short names that the receiver will recognise.
  // Source hierarchy (the spec is silent on the no-jsonldContext case;
  // we default to core and revisit if the ETSI TC clarifies):
  //   1) contextSourceInfo.jsonldContext (the CSR's explicit declaration,
  //      § 9 — "forwarding with this JSON-LD context using an appropriate
  //      binding").
  //   2) Core context (always present, neutral, never lies about a CSR's
  //      vocabulary).
  // Never NULL — emission sites can `swldCompact(forwardCtxP, iri)`
  // unconditionally and get back the short alias if available, or the
  // expanded IRI unchanged otherwise (then URL-encoded on the way out).
  itemP->forwardCtxP = NULL;
  if (itemP->contextSourceInfoKV != NULL)
  {
    for (int i = 0; itemP->contextSourceInfoKV[i] != NULL; i += 2)
    {
      if (strcasecmp(itemP->contextSourceInfoKV[i], "jsonldContext") == 0)
      {
        itemP->forwardCtxP = swldContextFromUrl(itemP->contextSourceInfoKV[i + 1], NULL);
        break;
      }
    }
  }
  if (itemP->forwardCtxP == NULL)
    itemP->forwardCtxP = swldCoreContext();

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

  if (itemP->probedAlias != NULL)
    free(itemP->probedAlias);

  if (itemP->regTree != NULL)
    kjFree(itemP->regTree);

  infoListFree(itemP->infoV);

  if (itemP->contextSourceInfoKV != NULL)
    free(itemP->contextSourceInfoKV);  // array only — strings are borrowed

  if (itemP->scopeV != NULL)
    free(itemP->scopeV);  // array only — strings are borrowed

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
    {
      if (strcmp(eiP->type, entityTypeV[i]) == 0) { typeMatch = true; break; }
    }
    if (!typeMatch)
      return false;
  }

  if (eiP->id == NULL && eiP->idPatternList == NULL)
    return true;

  // If caller passed NULL entityId (queryEntities — matching by type only),
  // any registration that covers this type matches regardless of id/idPattern.
  if (entityId == NULL)
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
  return ldRegCacheMatchForRetrieveScoped(cacheP, entityId, entityTypeV,
                                          NULL, modeFilter, matchVP);
}


int ldRegCacheMatchForRetrieveScoped(LdRegCache*       cacheP,
                                     const char*       entityId,
                                     char**            entityTypeV,
                                     char**            entityScopeV,
                                     LdRegMode         modeFilter,
                                     LdRegCacheItem*** matchVP)
{
  if (cacheP == NULL || matchVP == NULL)
    return 0;

  // Expired CSRs are dead — § 5.2.9 expiresAt. Compare once per call.
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t nowNs = (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;

  // Two-pass: count first, then allocate exactly + populate
  int count = 0;
  for (LdRegCacheItem* itemP = cacheP->itemList; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->mode != modeFilter)
      continue;
    if (itemP->expiresAt > 0 && itemP->expiresAt <= nowNs)
      continue;
    if (!csrScopeMatches(itemP->scopeV, entityScopeV))
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
    if (itemP->expiresAt > 0 && itemP->expiresAt <= nowNs)
      continue;
    if (!csrScopeMatches(itemP->scopeV, entityScopeV))
      continue;
    if (itemMatches(itemP, entityId, entityTypeV))
      v[ix++] = itemP;
  }

  *matchVP = v;
  return count;
}



// -----------------------------------------------------------------------------
//
// ldRegCacheLocalWriteConflict - § 9.3.3 guard for ?local=true writes
//
// A local write must not create data that an exclusive or redirect
// registration claims — the broker "holds no data locally in conflict to
// the registration". Distops-enabled writes enforce this implicitly (the
// claimed slice is chopped and forwarded); ?local=true skips the chop, so
// the guard has to run explicitly. Returns the regId of the first
// conflicting registration, or NULL.
//
//   entityId    - the written entity's id
//   entityTypeV - NULL-terminated expanded type IRIs (or NULL)
//   attrIriV    - NULL-terminated expanded attribute IRIs being written
//                 (NULL/empty = id+type-only write)
//
// Overlap rules (mirror of registration-time conflictCheck):
//   - RegInfo without entityInfoV covers any entity (attrs-only claim).
//   - RegInfo with neither propertyNames nor relationshipNames covers the
//     whole entity → any write to a matching entity conflicts.
//   - Attr-scoped RegInfo conflicts only when the write carries one of
//     the registered attributes.
//
const char* ldRegCacheLocalWriteConflict(LdRegCache* cacheP,
                                         const char* entityId,
                                         char**      entityTypeV,
                                         char**      entityScopeV,
                                         char**      attrIriV)
{
  if (cacheP == NULL || entityId == NULL)
    return NULL;

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t nowNs = (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;

  for (LdRegCacheItem* itemP = cacheP->itemList; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->mode != LdRegModeExclusive && itemP->mode != LdRegModeRedirect)
      continue;
    if (itemP->expiresAt > 0 && itemP->expiresAt <= nowNs)
      continue;
    if (!csrScopeMatches(itemP->scopeV, entityScopeV))
      continue;

    for (LdRegInfo* riP = itemP->infoV; riP != NULL; riP = riP->next)
    {
      bool entityCovered = (riP->entityInfoV == NULL);  // attrs-only claim: any entity

      for (LdRegEntityInfo* eiP = riP->entityInfoV; eiP != NULL && !entityCovered; eiP = eiP->next)
      {
        if (entityInfoMatches(eiP, entityId, entityTypeV))
          entityCovered = true;
      }
      if (!entityCovered)
        continue;

      // Whole-entity claim — every write to the entity conflicts
      if (riP->propertyNamesV == NULL && riP->relationshipNamesV == NULL)
        return itemP->regId;

      if (attrIriV == NULL)
        continue;

      for (int ix = 0; attrIriV[ix] != NULL; ix++)
      {
        if (riP->propertyNamesV != NULL)
        {
          for (int px = 0; riP->propertyNamesV[px] != NULL; px++)
          {
            if (strcmp(attrIriV[ix], riP->propertyNamesV[px]) == 0)
              return itemP->regId;
          }
        }
        if (riP->relationshipNamesV != NULL)
        {
          for (int rx = 0; riP->relationshipNamesV[rx] != NULL; rx++)
          {
            if (strcmp(attrIriV[ix], riP->relationshipNamesV[rx]) == 0)
              return itemP->regId;
          }
        }
      }
    }
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// ldRegCacheLocalWriteConflictTree - tree convenience over
// ldRegCacheLocalWriteConflict.
//
// Extracts the type IRIs, scope values and attribute IRIs from an
// (expanded) API entity/fragment tree and runs the § 9.3.3 local-write
// guard. 'id', 'type', 'scope' and @-prefixed members are entity
// keywords, not attributes.
//
const char* ldRegCacheLocalWriteConflictTree(LdRegCache* cacheP,
                                             const char* entityId,
                                             KjNode*     fragP,
                                             KAlloc*     kaP)
{
  if (cacheP == NULL || entityId == NULL || fragP == NULL || fragP->type != KjObject)
    return NULL;

  // type — string or array of strings
  KjNode* typeP      = kjLookup(fragP, "type");
  char*   typeBuf[2] = { NULL, NULL };
  char**  typeV      = NULL;
  if (typeP != NULL && typeP->type == KjString)
  {
    typeBuf[0] = typeP->value.s;
    typeV      = typeBuf;
  }
  else if (typeP != NULL && typeP->type == KjArray)
  {
    int n = 0;
    for (KjNode* tP = typeP->value.firstChildP; tP != NULL; tP = tP->next)
      if (tP->type == KjString) n++;
    if (n > 0)
    {
      typeV = (char**) kaAlloc(kaP, (n + 1) * sizeof(char*));
      int ix = 0;
      for (KjNode* tP = typeP->value.firstChildP; tP != NULL; tP = tP->next)
        if (tP->type == KjString) typeV[ix++] = tP->value.s;
      typeV[ix] = NULL;
    }
  }

  // scope — string or array of strings
  KjNode* scopeP      = kjLookup(fragP, "scope");
  char*   scopeBuf[2] = { NULL, NULL };
  char**  scopeV      = NULL;
  if (scopeP != NULL && scopeP->type == KjString)
  {
    scopeBuf[0] = scopeP->value.s;
    scopeV      = scopeBuf;
  }
  else if (scopeP != NULL && scopeP->type == KjArray)
  {
    int n = 0;
    for (KjNode* sP = scopeP->value.firstChildP; sP != NULL; sP = sP->next)
      if (sP->type == KjString) n++;
    if (n > 0)
    {
      scopeV = (char**) kaAlloc(kaP, (n + 1) * sizeof(char*));
      int ix = 0;
      for (KjNode* sP = scopeP->value.firstChildP; sP != NULL; sP = sP->next)
        if (sP->type == KjString) scopeV[ix++] = sP->value.s;
      scopeV[ix] = NULL;
    }
  }

  // attribute IRIs — every non-keyword member
  int attrN = 0;
  for (KjNode* aP = fragP->value.firstChildP; aP != NULL; aP = aP->next)
  {
    if (aP->name == NULL || aP->name[0] == '@')   continue;
    if (strcmp(aP->name, "id")    == 0)           continue;
    if (strcmp(aP->name, "type")  == 0)           continue;
    if (strcmp(aP->name, "scope") == 0)           continue;
    attrN++;
  }

  char** attrIriV = (char**) kaAlloc(kaP, (attrN + 1) * sizeof(char*));
  int    aIx      = 0;
  for (KjNode* aP = fragP->value.firstChildP; aP != NULL; aP = aP->next)
  {
    if (aP->name == NULL || aP->name[0] == '@')   continue;
    if (strcmp(aP->name, "id")    == 0)           continue;
    if (strcmp(aP->name, "type")  == 0)           continue;
    if (strcmp(aP->name, "scope") == 0)           continue;
    attrIriV[aIx++] = aP->name;
  }
  attrIriV[aIx] = NULL;

  return ldRegCacheLocalWriteConflict(cacheP, entityId, typeV, scopeV, attrIriV);
}



// ldRegOpSupported is now a static inline in ldRegCache.h — a single AND.



// -----------------------------------------------------------------------------
//
// attrInfoMatches - match attrsV against RegistrationInfo's
// propertyNames + relationshipNames (§ 5.12). Spec: if no Properties
// or Relationships are specified in the RegistrationInfo, the Attribute
// names are considered matching.
//
static bool attrInfoMatches(LdRegInfo* riP, char** attrsV)
{
  if (attrsV == NULL)                                          return true;
  if (riP->propertyNamesV == NULL && riP->relationshipNamesV == NULL)
    return true;

  for (int j = 0; attrsV[j] != NULL; j++)
  {
    if (riP->propertyNamesV != NULL)
    {
      for (int i = 0; riP->propertyNamesV[i] != NULL; i++)
        if (strcmp(riP->propertyNamesV[i], attrsV[j]) == 0) return true;
    }
    if (riP->relationshipNamesV != NULL)
    {
      for (int i = 0; riP->relationshipNamesV[i] != NULL; i++)
        if (strcmp(riP->relationshipNamesV[i], attrsV[j]) == 0) return true;
    }
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// ldRegInfoDiscoveryMatches - § 5.12 match of a single RegistrationInfo
// against the discovery filters: AND across (type, entityId, idRegex,
// attrsV). Public so getCsourceRegistrations can reuse the same logic
// for per-RegInfo response filtering (§ 5.10.2.5).
//
bool ldRegInfoDiscoveryMatches(LdRegInfo*     riP,
                               char**         entityIdV,
                               char**         typeV,
                               const regex_t* idRegex,
                               char**         attrsV)
{
  if (!attrInfoMatches(riP, attrsV))
    return false;

  // Spec: if no EntityInfo specified, entity constraint is considered matching.
  if (riP->entityInfoV == NULL)
    return true;

  for (LdRegEntityInfo* eiP = riP->entityInfoV; eiP != NULL; eiP = eiP->next)
  {
    // type check
    if (typeV != NULL && eiP->type != NULL)
    {
      bool typeOk = false;
      for (int i = 0; typeV[i] != NULL; i++)
        if (strcmp(eiP->type, typeV[i]) == 0) { typeOk = true; break; }
      if (!typeOk) continue;
    }

    // id exact-match filter: OR across entityIdV (any-of). Ignored when
    // the EntityInfo has no id constraint at all — the reg implicitly
    // covers any id of the matched type, so it matches. An EntityInfo
    // pinned by `id` must equal one of the queried ids; one constrained
    // by `idPattern` must have at least one queried id matching the
    // pattern (the reg cannot provide any other id — § 5.12).
    if (entityIdV != NULL && entityIdV[0] != NULL)
    {
      if (eiP->id != NULL)
      {
        bool idOk = false;
        for (int i = 0; entityIdV[i] != NULL; i++)
          if (strcmp(eiP->id, entityIdV[i]) == 0) { idOk = true; break; }
        if (!idOk) continue;
      }
      else if (eiP->idPatternList != NULL)
      {
        bool idOk = false;
        for (LdRegIdPattern* ripP = eiP->idPatternList; ripP != NULL && !idOk; ripP = ripP->next)
        {
          for (int i = 0; entityIdV[i] != NULL; i++)
            if (regexec(&ripP->regex, entityIdV[i], 0, NULL, 0) == 0) { idOk = true; break; }
        }
        if (!idOk) continue;
      }
    }

    // idPattern: request-side regex matched against EntityInfo.id (when
    // present). When the EntityInfo is itself idPattern-constrained there
    // is nothing to test — matching a regex against a regex is not
    // computable in general, so per ETSI agreement the two always match.
    if (idRegex != NULL && eiP->id != NULL)
    {
      if (regexec(idRegex, eiP->id, 0, NULL, 0) != 0)
        continue;
    }

    return true;
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// itemDiscoveryMatches - CSR matches if ANY RegistrationInfo matches.
//
static bool itemDiscoveryMatches(LdRegCacheItem* itemP,
                                 char**          entityIdV,
                                 char**          typeV,
                                 const regex_t*  idRegex,
                                 char**          attrsV)
{
  for (LdRegInfo* riP = itemP->infoV; riP != NULL; riP = riP->next)
  {
    if (ldRegInfoDiscoveryMatches(riP, entityIdV, typeV, idRegex, attrsV))
      return true;
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// ldRegCacheMatchForDiscovery - § 5.10.2 Query Context Source Registrations.
//
// Walks every non-expired CSR in the cache regardless of mode and returns
// those with at least one RegistrationInfo matching the filters. Caller
// frees *matchVP with free().
//
// typeV:      NULL = ignore type filter; else NULL-terminated expanded IRIs
// entityId:   NULL = ignore id filter; else exact URI
// idPattern:  NULL = ignore; else POSIX regex, matched request-side against
//             each EntityInfo.id (spec § 6.8.3.2).
// attrsV:     NULL = ignore; else NULL-terminated expanded attribute IRIs
//             matched against RegistrationInfo.propertyNames / relationshipNames.
//
int ldRegCacheMatchForDiscovery(LdRegCache*       cacheP,
                                char**            typeV,
                                char**            entityIdV,
                                const char*       idPattern,
                                char**            attrsV,
                                LdRegCacheItem*** matchVP)
{
  if (cacheP == NULL || matchVP == NULL)
    return 0;

  regex_t  idRegex;
  bool     haveRegex = false;
  if (idPattern != NULL && idPattern[0] != 0)
  {
    if (regcomp(&idRegex, idPattern, REG_EXTENDED | REG_NOSUB) == 0)
      haveRegex = true;
  }

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t nowNs = (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;

  int count = 0;
  for (LdRegCacheItem* itemP = cacheP->itemList; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->expiresAt > 0 && itemP->expiresAt <= nowNs)
      continue;
    if (itemDiscoveryMatches(itemP, entityIdV, typeV,
                              haveRegex ? &idRegex : NULL, attrsV))
      count++;
  }

  if (count == 0)
  {
    if (haveRegex) regfree(&idRegex);
    return 0;
  }

  LdRegCacheItem** v = (LdRegCacheItem**) malloc(count * sizeof(LdRegCacheItem*));
  int ix = 0;

  for (LdRegCacheItem* itemP = cacheP->itemList; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->expiresAt > 0 && itemP->expiresAt <= nowNs)
      continue;
    if (itemDiscoveryMatches(itemP, entityIdV, typeV,
                              haveRegex ? &idRegex : NULL, attrsV))
      v[ix++] = itemP;
  }

  if (haveRegex) regfree(&idRegex);

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
