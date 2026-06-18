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
#include "swNgsild/SwNgsild.h"                          // swNgsild (per-conn probePending cache)
#include "swNgsild/ldCheckDateTime.h"                  // ldIsoToNanoseconds
#include "swNgsild/ldScopeMatch.h"                     // ldScopePatternMatch
#include "swNgsild/ldProbeSourceIdentity.h"            // ldProbeSourceIdentity
#include "swNgsild/ldRegCache.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// Deferred source-identity probing — the probe is a best-effort network
// call; deferring it off the request path makes CSR create/update latency
// independent of the registered endpoint's reachability. The queue is
// thread-local: items are enqueued while the request executes and drained
// by ldRegCacheProbePending() right after the response goes out, on the
// same thread (the broker's one-request-per-thread invariant).
//
// The queue holds (cache, regId) — never the item pointer: the response
// has gone out by drain time, and the registration may have been deleted
// (and its item freed) by another thread in between. The drain re-looks
// the item up by regId and silently skips a vanished one.
//
// The drain writes itemP->csourceAlias while the item is visible in the
// shared cache: a single aligned pointer store — concurrent readers see
// either NULL (probe not done: Via-based detection covers them) or the
// complete alias.
//
// Inc6c — the probe queue lives in the per-connection swNgsild
// (probePendingV[PROBE_PENDING_MAX] / probePendingN); strdup'd regIds are freed
// when drained below (post-response hook).
//
static void probePendingAdd(LdRegCache* cacheP, LdRegCacheItem* itemP)
{
  if (swNgsild.probePendingN >= PROBE_PENDING_MAX || itemP->regId == NULL)
    return;  // skip the probe — csourceAlias stays NULL, Via detection covers

  swNgsild.probePendingV[swNgsild.probePendingN].cacheP = cacheP;
  swNgsild.probePendingV[swNgsild.probePendingN].regId  = strdup(itemP->regId);
  swNgsild.probePendingN++;
}

void ldRegCacheProbePending(void)
{
  for (int i = 0; i < swNgsild.probePendingN; i++)
  {
    LdRegCacheItem* itemP = ldRegCacheItemLookup(swNgsild.probePendingV[i].cacheP, swNgsild.probePendingV[i].regId);

    if (itemP != NULL && itemP->endpoint != NULL && itemP->csourceAlias == NULL)
    {
      itemP->probedAlias  = ldProbeSourceIdentity(itemP->endpoint, itemP->tenant, 0);
      itemP->csourceAlias = itemP->probedAlias;   // may be NULL on failure
    }

    free(swNgsild.probePendingV[i].regId);
  }
  swNgsild.probePendingN = 0;
}



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
    KjNode* attrNamesP  = kjLookup(infoP, "attributeNames");

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

    riP->attributeNamesV = attrIRIArrayExtract(attrNamesP, allocP);

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

    if (head->attributeNamesV != NULL)  free(head->attributeNamesV);

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
  pthread_rwlock_init(&cacheP->lock, NULL);

  return cacheP;
}



// -----------------------------------------------------------------------------
//
// ldRegCacheRdLock / ldRegCacheWrLock / ldRegCacheUnlock -
//
// Caller-held locking for the per-tenant registration cache. The MatchFor*
// readers take the rdlock only for the brief list WALK and pin (refcount++)
// the items they return, then drop the lock — the caller forwards lock-free and
// calls ldRegCacheMatchRelease to unpin. Writers (CSR POST/PATCH/DELETE) take
// the wrlock around the whole read-modify-write so it is one atomic unit, and
// ldRegCacheItemLookup stays a pure caller-holds-lock primitive (writers call
// it under the wrlock; readers under the rdlock) so there is no self-deadlock.
// NULL-safe (some call sites guard a NULL cache).
//
void ldRegCacheRdLock(LdRegCache* cacheP) { if (cacheP != NULL) pthread_rwlock_rdlock(&cacheP->lock); }
void ldRegCacheWrLock(LdRegCache* cacheP) { if (cacheP != NULL) pthread_rwlock_wrlock(&cacheP->lock); }
void ldRegCacheUnlock(LdRegCache* cacheP) { if (cacheP != NULL) pthread_rwlock_unlock(&cacheP->lock); }



// -----------------------------------------------------------------------------
//
// Refcount-pin bookkeeping (see LdRegCacheItem.refCount). Only writers free,
// and only under the wrlock; readers ever only atomic-increment (pin, under the
// rdlock) / atomic-decrement (unpin, lock-free after the forward).
//
static void cacheItemFree(LdRegCacheItem* itemP);   // fwd decl (defined below)

void ldRegCacheItemPin(LdRegCacheItem* itemP)
{
  if (itemP != NULL)
    __atomic_add_fetch(&itemP->refCount, 1, __ATOMIC_SEQ_CST);
}

void ldRegCacheMatchRelease(LdRegCacheItem** matchV, int n)
{
  if (matchV == NULL)
    return;
  for (int i = 0; i < n; i++)
    __atomic_sub_fetch(&matchV[i]->refCount, 1, __ATOMIC_SEQ_CST);
  free(matchV);
}

//
// ldRegCacheItemUnpin - unpin a single matched item WITHOUT freeing the array.
// For callers that filter a matchV in place (drop some matches before the
// forward): unpin each dropped item here so the later ldRegCacheMatchRelease,
// called with the now-reduced count, still balances every pin.
//
void ldRegCacheItemUnpin(LdRegCacheItem* itemP)
{
  if (itemP != NULL)
    __atomic_sub_fetch(&itemP->refCount, 1, __ATOMIC_SEQ_CST);
}

//
// cacheReapRetired - free retired (unlinked) items whose pin count has fallen
// back to 0. Caller MUST hold the wrlock. Called at the head of every writer
// op so retired items are reclaimed promptly once their last reader unpins.
//
static void cacheReapRetired(LdRegCache* cacheP)
{
  LdRegCacheItem* itemP = cacheP->retiredList;
  LdRegCacheItem* prevP = NULL;

  while (itemP != NULL)
  {
    LdRegCacheItem* nextP = itemP->next;

    if (__atomic_load_n(&itemP->refCount, __ATOMIC_SEQ_CST) == 0)
    {
      if (prevP == NULL) cacheP->retiredList = nextP;
      else               prevP->next         = nextP;
      cacheItemFree(itemP);
    }
    else
      prevP = itemP;

    itemP = nextP;
  }
}

//
// cacheItemRetireOrFree - the item is already unlinked from itemList. If no
// reader has it pinned, free it now; otherwise park it on retiredList for a
// later reap. Caller holds the wrlock.
//
static void cacheItemRetireOrFree(LdRegCache* cacheP, LdRegCacheItem* itemP)
{
  if (__atomic_load_n(&itemP->refCount, __ATOMIC_SEQ_CST) == 0)
    cacheItemFree(itemP);
  else
  {
    itemP->retired = true;
    itemP->next    = cacheP->retiredList;
    cacheP->retiredList = itemP;
  }
}



// -----------------------------------------------------------------------------
//
// ldRegCacheItemAdd -
//
LdRegCacheItem* ldRegCacheItemAdd(LdRegCache* cacheP, KjNode* regTree)
{
  if (cacheP == NULL || regTree == NULL)
    return NULL;

  cacheReapRetired(cacheP);   // caller holds the wrlock (or single-threaded at load)

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
  // detection). If the client didn't provide one, the CSR's
  // /info/sourceIdentity endpoint (§ 5.15) is probed to learn it — but
  // NOT here: the probe is a network call (bounded, but an unreachable
  // endpoint still costs the full timeout) and must not stall the CSR
  // create/update response. The item queues for ldRegCacheProbePending(),
  // which runs post-response on this same thread. Until then (and on
  // probe failure) csourceAlias stays NULL — reactive Via-based
  // detection still protects us.
  KjNode* aliasP = kjLookup(itemP->regTree, "contextSourceAlias");
  if (aliasP != NULL && aliasP->type == KjString)
  {
    itemP->csourceAlias = aliasP->value.s;
    itemP->probedAlias  = NULL;
  }
  else if (itemP->endpoint != NULL)
    probePendingAdd(cacheP, itemP);

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
// Caller MUST hold the wrlock. Unlinks the item and frees it — UNLESS a reader
// still has it pinned (refcount > 0), in which case it is parked on retiredList
// and reclaimed by a later reap. Also reaps previously-retired items first.
bool ldRegCacheItemRemove(LdRegCache* cacheP, const char* regId)
{
  if (cacheP == NULL || regId == NULL)
    return false;

  cacheReapRetired(cacheP);

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

      cacheItemRetireOrFree(cacheP, itemP);
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

  // Two-pass: count first, then allocate exactly + populate. Both passes under
  // the rdlock so the list is stable; each returned item is pinned so the caller
  // can forward lock-free (release with ldRegCacheMatchRelease).
  ldRegCacheRdLock(cacheP);

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
  {
    ldRegCacheUnlock(cacheP);
    return 0;
  }

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
    {
      v[ix++] = itemP;
      ldRegCacheItemPin(itemP);
    }
  }

  ldRegCacheUnlock(cacheP);
  *matchVP = v;
  return count;
}



// -----------------------------------------------------------------------------
//
// entityInfoQueryMatches - one EntityInfo vs a QUERY's id selectors.
//
// See ldRegCacheMatchForQuery's header comment for the matching grid.
// idV and idRegex may both be present (?id= AND ?idPattern= combine as
// conjunctive constraints on the entity): a pinned registration id must
// then satisfy both; for an idPattern-constrained EntityInfo only the
// concrete queried ids are testable — the query-pattern side is
// regex-vs-regex and always matches per ETSI agreement.
//
static bool entityInfoQueryMatches(LdRegEntityInfo* eiP,
                                   char**           idV,
                                   const regex_t*   idRegex,
                                   char**           typeV)
{
  if (typeV != NULL && eiP->type != NULL)
  {
    bool typeOk = false;
    for (int i = 0; typeV[i] != NULL; i++)
      if (strcmp(eiP->type, typeV[i]) == 0) { typeOk = true; break; }
    if (!typeOk)
      return false;
  }

  // No id constraint on the EntityInfo — it covers any id of the type.
  if (eiP->id == NULL && eiP->idPatternList == NULL)
    return true;

  // No id selector on the query — type-only matching.
  if (idV == NULL && idRegex == NULL)
    return true;

  // Pinned registration id: must satisfy every query selector present.
  if (eiP->id != NULL)
  {
    bool ok = true;

    if (idV != NULL)
    {
      ok = false;
      for (int i = 0; idV[i] != NULL; i++)
        if (strcmp(eiP->id, idV[i]) == 0) { ok = true; break; }
    }

    if (ok && idRegex != NULL)
      ok = (regexec(idRegex, eiP->id, 0, NULL, 0) == 0);

    if (ok)
      return true;
  }

  // idPattern-constrained EntityInfo: test the decidable side (queried
  // concrete ids against the registration's patterns). When the query
  // carries only ?idPattern=, this is regex-vs-regex → always matches.
  if (eiP->idPatternList != NULL)
  {
    if (idV == NULL)
      return true;

    for (LdRegIdPattern* ripP = eiP->idPatternList; ripP != NULL; ripP = ripP->next)
    {
      for (int i = 0; idV[i] != NULL; i++)
        if (regexec(&ripP->regex, idV[i], 0, NULL, 0) == 0) return true;
    }
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// ldRegCacheMatchForQuery -
//
int ldRegCacheMatchForQuery(LdRegCache*       cacheP,
                            char**            entityIdV,
                            const char*       idPattern,
                            char**            entityTypeV,
                            LdRegMode         modeFilter,
                            LdRegCacheItem*** matchVP)
{
  if (cacheP == NULL || matchVP == NULL)
    return 0;

  regex_t idRegex;
  bool    haveIdRegex = false;
  if (idPattern != NULL && idPattern[0] != 0)
  {
    if (regcomp(&idRegex, idPattern, REG_EXTENDED | REG_NOSUB) == 0)
      haveIdRegex = true;
  }

  char** idV = (entityIdV != NULL && entityIdV[0] != NULL) ? entityIdV : NULL;

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t nowNs = (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;

  // Two-pass: count first, then allocate exactly + populate. Under the rdlock
  // (list stable across both passes); each returned item is pinned for the
  // caller's lock-free forward (release via ldRegCacheMatchRelease).
  ldRegCacheRdLock(cacheP);
  int count = 0;
  for (int pass = 0; pass < 2; pass++)
  {
    LdRegCacheItem** v  = NULL;
    int              ix = 0;

    if (pass == 1)
    {
      if (count == 0)
        break;
      v = (LdRegCacheItem**) malloc(count * sizeof(LdRegCacheItem*));
    }

    for (LdRegCacheItem* itemP = cacheP->itemList; itemP != NULL; itemP = itemP->next)
    {
      if (itemP->mode != modeFilter)
        continue;
      if (itemP->expiresAt > 0 && itemP->expiresAt <= nowNs)
        continue;

      bool match = false;
      for (LdRegInfo* riP = itemP->infoV; riP != NULL && !match; riP = riP->next)
      {
        if (riP->entityInfoV == NULL)
        {
          match = true;  // no entity constraint at all — matches
          continue;
        }
        for (LdRegEntityInfo* eiP = riP->entityInfoV; eiP != NULL; eiP = eiP->next)
          if (entityInfoQueryMatches(eiP, idV, haveIdRegex ? &idRegex : NULL, entityTypeV)) { match = true; break; }
      }

      if (!match)
        continue;

      if (pass == 0) count++;
      else         { v[ix++] = itemP; ldRegCacheItemPin(itemP); }
    }

    if (pass == 1)
      *matchVP = v;
  }
  ldRegCacheUnlock(cacheP);

  if (haveIdRegex)
    regfree(&idRegex);

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

  // rdlock the walk so a concurrent CSR write can't mutate the list under us.
  // The returned regId is borrowed (used synchronously for the § 9.3.3 409
  // detail, no forward), so we don't pin — the post-return window is a few
  // instructions with no I/O.
  ldRegCacheRdLock(cacheP);

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
      if (riP->attributeNamesV == NULL)
      { ldRegCacheUnlock(cacheP); return itemP->regId; }

      if (attrIriV == NULL)
        continue;

      for (int ix = 0; attrIriV[ix] != NULL; ix++)
      {
        for (int ax = 0; riP->attributeNamesV[ax] != NULL; ax++)
        {
          if (strcmp(attrIriV[ix], riP->attributeNamesV[ax]) == 0)
          { ldRegCacheUnlock(cacheP); return itemP->regId; }
        }
      }
    }
  }

  ldRegCacheUnlock(cacheP);
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
// attrInfoMatches - match attrsV against RegistrationInfo's attributeNames
// (§ 5.12). Spec: if no attribute names are specified in the RegistrationInfo,
// the Attribute names are considered matching.
//
static bool attrInfoMatches(LdRegInfo* riP, char** attrsV)
{
  if (attrsV == NULL)                  return true;
  if (riP->attributeNamesV == NULL)    return true;

  for (int j = 0; attrsV[j] != NULL; j++)
  {
    for (int i = 0; riP->attributeNamesV[i] != NULL; i++)
      if (strcmp(riP->attributeNamesV[i], attrsV[j]) == 0) return true;
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

  // Both passes under the rdlock; each returned item pinned for the caller's
  // lock-free use (release via ldRegCacheMatchRelease).
  ldRegCacheRdLock(cacheP);

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
    ldRegCacheUnlock(cacheP);
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
    {
      v[ix++] = itemP;
      ldRegCacheItemPin(itemP);
    }
  }

  ldRegCacheUnlock(cacheP);
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

  // retired-but-not-yet-reaped items (any still-pinned ones are being released
  // by readers that are about to finish — release runs at teardown).
  itemP = cacheP->retiredList;
  while (itemP != NULL)
  {
    LdRegCacheItem* nextP = itemP->next;
    cacheItemFree(itemP);
    itemP = nextP;
  }

  pthread_rwlock_destroy(&cacheP->lock);
  free(cacheP);
}
