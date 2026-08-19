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

#include "ktrace/kTrace.h"                             // KT_RE
#include "kalloc/kaBufferInit.h"                       // kaBufferInit
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjClone.h"                             // kjClone
#include "kjson/kjFree.h"                              // kjFree
#include "kjson/kjLookup.h"                            // kjLookup
#include "kjson/kjBuilder.h"                           // kjChildRemove
#include "kjson/kjRender.h"                            // kjFastRender
#include "kjson/kjRenderSize.h"                        // kjFastRenderSize

#include "corNgsild/ldTypes.h"                          // ldFormatFromString
#include "corRest/CorRestIn.h"                    // corAcceptParse
#include "corNgsild/CorNgsild.h"                         // corNgsild (contextP)
#include "corNgsild/LdVocab.h"                          // LD_VOCAB_*
#include "corNgsild/LdSubCache.h"                       // LdSubCache, LdSubCacheItem
#include "corNgsild/ldThrottleDirty.h"                  // ldThrottleDirtyFree
#include "corNgsild/LdScopeExpr.h"                      // ldScopeExprParse
#include "corNgsild/LdGeoRel.h"                         // ldGeoRelParse
#include "corNgsild/LdTypeExpr.h"                       // ldTypeExprParse
#include "corNgsild/ldCheckDateTime.h"                  // ldCheckDateTime
#include "corJsonld/corLdExpand.h"                       // corLdExpand, corLdAlreadyExpanded
#include "corNgsild/ldSubscriptionNotify.h"              // ldTriggerFromString
#include "corNgsild/ldConformanceDowngrade.h"            // ldConformanceParse
#include "corNgsild/ldQParse.h"                         // ldQParse
#include "corNgsild/ldSubCache.h"                       // Own interface



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

  // ldCheckSubscription has already parsed each entities[].type into a
  // malloc-allocated LdTypeExpr and stashed it on corNgsild. Claim the
  // tree here (zero the slot so corNgsildReset doesn't free-double).
  int selIx = 0;
  for (KjNode* selP = entitiesP->value.firstChildP; selP != NULL; selP = selP->next, selIx++)
  {
    if (selP->type != KjObject)
      continue;

    LdSubEntitySelector* esP = (LdSubEntitySelector*) calloc(1, sizeof(LdSubEntitySelector));

    // type — raw text kept for diagnostics. The parsed §4.17 tree
    // comes from the parse-once side channel populated during
    // validation; the cache owns it from here on.
    KjNode* typeP = kjLookup(selP, "type");
    if (typeP != NULL && typeP->type == KjString)
    {
      esP->type = typeP->value.s;
      if (corNgsild.subEntityTypeExprsV != NULL && selIx < corNgsild.subEntityTypeExprsN)
      {
        esP->typeExpr = corNgsild.subEntityTypeExprsV[selIx];
        corNgsild.subEntityTypeExprsV[selIx] = NULL;
      }
    }

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
        ripP->source       = patP->value.s;   // borrowed from cloned subTree
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
// ldSubCacheSubordinatesFree - free the linked list of derived-sub mappings
//
void ldSubCacheSubordinatesFree(LdSubSubordinate* head)
{
  while (head != NULL)
  {
    LdSubSubordinate* next = head->next;
    if (head->remoteSubId != NULL) free(head->remoteSubId);
    if (head->regId       != NULL) free(head->regId);
    free(head);
    head = next;
  }
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

    // Parsed §4.17 expression is malloc-allocated (caller passed NULL
    // to ldTypeExprParse so the tree survives request boundaries).
    ldTypeExprFree(head->typeExpr);

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
  pthread_rwlock_init(&cacheP->lock, NULL);

  return cacheP;
}



// -----------------------------------------------------------------------------
//
// ldSubCacheRdLock / ldSubCacheWrLock / ldSubCacheUnlock - caller-held locking
// (mirror of the reg cache). Readers (notify drain, CSR-sub matchers) rdlock the
// walk and pin items they use across a notification send; writers (sub CRUD)
// wrlock the list mutation. LOCK ORDER: reg lock BEFORE sub lock. NULL-safe.
//
void ldSubCacheRdLock(LdSubCache* cacheP) { if (cacheP != NULL) pthread_rwlock_rdlock(&cacheP->lock); }
void ldSubCacheWrLock(LdSubCache* cacheP) { if (cacheP != NULL) pthread_rwlock_wrlock(&cacheP->lock); }
void ldSubCacheUnlock(LdSubCache* cacheP) { if (cacheP != NULL) pthread_rwlock_unlock(&cacheP->lock); }



// -----------------------------------------------------------------------------
//
// Refcount-pin bookkeeping (mirror of the reg cache). Only writers free, under
// the wrlock; readers ever only atomic-increment (pin, under rdlock) / decrement
// (unpin, lock-free after the send).
//
static void cacheItemFree(LdSubCacheItem* itemP);   // fwd decl (defined below)

void ldSubCacheItemPin(LdSubCacheItem* itemP)
{
  if (itemP != NULL)
    __atomic_add_fetch(&itemP->refCount, 1, __ATOMIC_SEQ_CST);
}

void ldSubCacheItemUnpin(LdSubCacheItem* itemP)
{
  if (itemP != NULL)
    __atomic_sub_fetch(&itemP->refCount, 1, __ATOMIC_SEQ_CST);
}

// Free retired (unlinked) items whose pin count has fallen to 0. Caller holds
// the wrlock. Called at the head of every writer op.
static void cacheReapRetired(LdSubCache* cacheP)
{
  LdSubCacheItem* itemP = cacheP->retiredList;
  LdSubCacheItem* prevP = NULL;

  while (itemP != NULL)
  {
    LdSubCacheItem* nextP = itemP->next;

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

// The item is already unlinked from itemList. Free it now if unpinned, else park
// on retiredList for a later reap. Caller holds the wrlock.
static void cacheItemRetireOrFree(LdSubCache* cacheP, LdSubCacheItem* itemP)
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
// riHeaderValue - literal value of a receiverInfo header by case-insensitive name
//
// Returns NULL if absent or set to the urn:ngsi-ld:request substitution sentinel
// (a per-notification value, not usable as a static notification parameter).
//
static const char* riHeaderValue(KjNode* riP, const char* name)
{
  if (riP == NULL || riP->type != KjArray)
    return NULL;

  for (KjNode* kvP = riP->value.firstChildP; kvP != NULL; kvP = kvP->next)
  {
    if (kvP->type != KjObject) continue;
    KjNode* kP = kjLookup(kvP, "key");
    KjNode* vP = kjLookup(kvP, "value");
    if (kP == NULL || kP->type != KjString || vP == NULL || vP->type != KjString) continue;
    if (strcasecmp(kP->value.s, name) == 0)
      return (strcmp(vP->value.s, "urn:ngsi-ld:request") == 0) ? NULL : vP->value.s;
  }
  return NULL;
}



// -----------------------------------------------------------------------------
//
// ldSubCacheItemAdd -
//
LdSubCacheItem* ldSubCacheItemAdd(LdSubCache* cacheP, KjNode* subTree, LdQNode* qExpr, LdFormat format)
{
  if (cacheP == NULL || subTree == NULL)
    return NULL;

  cacheReapRetired(cacheP);   // caller holds the wrlock (or single-threaded at load)

  LdSubCacheItem* itemP = (LdSubCacheItem*) calloc(1, sizeof(LdSubCacheItem));

  // § 5.2.x throttling — guards the coalesce-to-latest dirty set (buffered into
  // by request threads under the cache RDLOCK, drained by the periodic flush).
  pthread_mutex_init(&itemP->dirtyLock, NULL);

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
  // applied by corLdExpandTree, so the strings stay short after parseHook.
  // Downstream match paths (entity-side notify, CSR-side notify) compare
  // against expanded IRIs, so expand once here at cache-ingest time.
  //
  // Use the REQUEST's @context (corNgsild.contextP) — passing NULL would
  // fall back to the broker's core context which doesn't define
  // user-vocab terms like "name". For sub-create paths corNgsild.contextP
  // is the @context the client supplied; for cache-reload at boot the
  // sub's persisted jsonldContext URL has already been resolved into
  // corNgsild.contextP by the reload driver. Without this, ETSI 046_22_*
  // tests with watchedAttributes silently never match attributeDeleted
  // events because the cache stores "name" while the merge report carries
  // the IRI "https://ngsi-ld-test-suite/context#name".
  if (itemP->watchedAttrsV != NULL)
  {
    for (int i = 0; itemP->watchedAttrsV[i] != NULL; i++)
    {
      if (!corLdAlreadyExpanded(itemP->watchedAttrsV[i]))
        itemP->watchedAttrsV[i] = corLdExpand(corNgsild.contextP, itemP->watchedAttrsV[i],
                                             &cacheP->alloc, NULL, NULL);
    }
  }

  // Use pre-parsed tree if provided, otherwise parse from the stored expanded q-string.
  // Note: ldQParse called here will re-expand attrs via corNgsild.contextP, but since
  // the attrs are already expanded IRIs (contain "://"), corLdExpand returns them unchanged.
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

  // csf — the Context Source Filter. Only a Context Source Registration
  // Subscription consults it (§ 5.2.12); parsing it is harmless either way.
  KjNode* csfNodeP = kjLookup(itemP->subTree, "csf");
  if (csfNodeP != NULL && csfNodeP->type == KjString)
    itemP->csfExpr = ldQParse(csfNodeP->value.s, &cacheP->alloc);

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
    else if (coordsP != NULL && coordsP->type == KjArray)
    {
      // coordinates given as a native JSON array (GeoJSON form, e.g.
      // [[[lon,lat],...]]) — render it to the JSON-array string that
      // geoMatchFunc/geojsonToGeos expects. Stored in the cache alloc.
      int   len = kjFastRenderSize(coordsP) + 1;
      char* buf = (char*) kaAlloc(&cacheP->alloc, len);
      if (buf != NULL)
      {
        kjFastRender(coordsP, buf);
        itemP->geoCoordinates = buf;
      }
    }

    if (geopropP != NULL && geopropP->type == KjString)
    {
      // The value may be a short name (e.g. "location") — expand it
      if (corLdAlreadyExpanded(geopropP->value.s))
        itemP->geoProperty = geopropP->value.s;
      else
        itemP->geoProperty = corLdExpand(NULL, geopropP->value.s, &cacheP->alloc, NULL, NULL);
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
  itemP->status = ldSubStatusFromString((statusP != NULL && statusP->type == KjString) ? statusP->value.s : NULL);

  // 'notification' is mandatory (validated on every write). A subscription that
  // reaches the cache without one means the stored document is corrupt — refuse
  // to cache it rather than silently fabricate defaults for the missing members.
  KjNode* notifP = kjLookup(itemP->subTree, LD_VOCAB_NOTIFICATION);
  if (notifP == NULL)
    KT_RE(NULL, "Corrupted DB: subscription '%s' has no 'notification' (a mandatory member) — refusing to cache it",
          itemP->subId ? itemP->subId : "?");

  KjNode* endpointP = kjLookup(notifP, LD_VOCAB_ENDPOINT);
  KjNode* uriP      = (endpointP != NULL) ? kjLookup(endpointP, LD_VOCAB_URI) : NULL;
  itemP->endpointUri = (uriP != NULL && uriP->type == KjString) ? uriP->value.s : NULL;

  // § 5.2.15 endpoint.accept — application/json | application/ld+json |
  // application/geo+json. Accepted both expanded and short forms.
  KjNode* acceptP = NULL;
  if (endpointP != NULL)
  {
    acceptP = kjLookup(endpointP, "https://uri.etsi.org/ngsi-ld/accept");
    if (acceptP == NULL) acceptP = kjLookup(endpointP, "accept");
  }
  itemP->endpointAccept = corAcceptParse((acceptP != NULL && acceptP->type == KjString) ? acceptP->value.s : NULL);

  // A receiverInfo Content-Type acts as endpoint.accept when accept is absent
  // (validated consistent in ldCheckSubscription). It is then NOT re-emitted as
  // a receiverInfo header — the broker emits this single Content-Type.
  if (acceptP == NULL && endpointP != NULL)
  {
    const char* riCt = riHeaderValue(kjLookup(endpointP, "receiverInfo"), "Content-Type");
    if (riCt != NULL)
      itemP->endpointAccept = corAcceptParse(riCt);
  }

  // § 5.2.12 / § 4.3.6.8 ngsildConformance — back-compat target version
  KjNode* ncP = kjLookup(itemP->subTree, "ngsildConformance");
  itemP->conformanceMajor = 0;
  itemP->conformanceMinor = 0;
  if (ncP != NULL && ncP->type == KjString)
    ldConformanceParse(ncP->value.s, &itemP->conformanceMajor, &itemP->conformanceMinor);

  // § 5.2.15 endpoint.cooldown — minimum delay (ms) before retrying after
  // a failure on the same endpoint. Convert to ns; 0 means "use the default".
  KjNode* coolP = (endpointP != NULL) ? kjLookup(endpointP, "cooldown") : NULL;
  if (coolP != NULL && (coolP->type == KjInt || coolP->type == KjFloat))
  {
    double ms = (coolP->type == KjInt) ? (double) coolP->value.i : coolP->value.f;
    if (ms > 0)
      itemP->cooldownNs = (uint64_t) (ms * 1000000.0);
  }

  // § 5.2.15 endpoint.timeout — maximum ms to wait for a notification reply.
  KjNode* tmoP = (endpointP != NULL) ? kjLookup(endpointP, "timeout") : NULL;
  if (tmoP != NULL && (tmoP->type == KjInt || tmoP->type == KjFloat))
  {
    double ms = (tmoP->type == KjInt) ? (double) tmoP->value.i : tmoP->value.f;
    if (ms > 0)
      itemP->timeoutMs = (int) ms;
  }

  // § 5.2.15 endpoint.receiverInfo — KeyValuePair[] forwarded as outbound headers.
  KjNode* riP = (endpointP != NULL) ? kjLookup(endpointP, "receiverInfo") : NULL;
  if (riP != NULL && riP->type == KjArray)
    itemP->receiverInfo = riP;

  // § 5.2.15 endpoint.notifierInfo — KeyValuePair[] used by transport-
  // specific protocol parameters (e.g. MQTT-QoS, MQTT-Version per § 7.2).
  KjNode* niP = (endpointP != NULL) ? kjLookup(endpointP, "notifierInfo") : NULL;
  if (niP != NULL && niP->type == KjArray)
    itemP->notifierInfo = niP;

  // § 5.2.14 notification.join + notification.joinLevel — linked-entity retrieval (§ 4.5.23)
  KjNode* joinP      = (notifP != NULL) ? kjLookup(notifP, "join")      : NULL;
  KjNode* joinLevelP = (notifP != NULL) ? kjLookup(notifP, "joinLevel") : NULL;
  if (joinP != NULL && joinP->type == KjString)
  {
    itemP->notifJoin       = joinP->value.s;
    itemP->notifJoinActive = (strcmp(joinP->value.s, "@none") != 0);
  }
  if (joinLevelP != NULL && joinLevelP->type == KjInt && joinLevelP->value.i > 0)
    itemP->notifJoinLevel = (int) joinLevelP->value.i;

  KjNode* notifAttrsP = (notifP != NULL) ? kjLookup(notifP, LD_VOCAB_ATTRIBUTES) : NULL;
  itemP->notifAttrsV = watchedAttrsExtract(notifAttrsP);  // reuse same helper (NULL-term string array)

  // Expand short names in notifAttrsV using the request's @context — see
  // the watchedAttrsV block above for rationale.
  if (itemP->notifAttrsV != NULL)
  {
    for (int i = 0; itemP->notifAttrsV[i] != NULL; i++)
    {
      if (!corLdAlreadyExpanded(itemP->notifAttrsV[i]))
        itemP->notifAttrsV[i] = corLdExpand(corNgsild.contextP, itemP->notifAttrsV[i],
                                           &cacheP->alloc, NULL, NULL);
    }
  }

  // notification.pick / notification.omit (§ 5.2.14, § 4.21). Same expansion
  // semantics as notifAttrsV — short attribute names get IRI-expanded; the
  // "id"/"type"/"scope" keywords pass through unchanged.
  KjNode* pickP = (notifP != NULL) ? kjLookup(notifP, "pick") : NULL;
  KjNode* omitP = (notifP != NULL) ? kjLookup(notifP, "omit") : NULL;
  itemP->notifPickV = watchedAttrsExtract(pickP);
  itemP->notifOmitV = watchedAttrsExtract(omitP);
  for (int j = 0; j < 2; j++)
  {
    char** v = (j == 0) ? itemP->notifPickV : itemP->notifOmitV;
    if (v == NULL) continue;
    for (int i = 0; v[i] != NULL; i++)
    {
      if (strcmp(v[i], "id") == 0 || strcmp(v[i], "type") == 0 || strcmp(v[i], "scope") == 0) continue;
      if (!corLdAlreadyExpanded(v[i]))
        v[i] = corLdExpand(corNgsild.contextP, v[i], &cacheP->alloc, NULL, NULL);
    }
  }

  // Subscription-level lang (§ 4.15) — applied to LanguageMap attrs at notify time.
  KjNode* langP = kjLookup(itemP->subTree, "lang");
  itemP->lang = (langP != NULL && langP->type == KjString) ? langP->value.s : NULL;

  // datasetId: list of dataset IDs to include in notifications (NULL = all instances)
  // Values are URIs or "@none" (default instance) — no expansion needed.
  KjNode* datasetIdP = kjLookup(itemP->subTree, LD_VOCAB_DATASET_ID);
  itemP->datasetIdV = watchedAttrsExtract(datasetIdP);  // reuse: builds NULL-term string array

  KjNode* expiresP = kjLookup(itemP->subTree, LD_VOCAB_EXPIRES_AT);
  if (expiresP != NULL && expiresP->type == KjString)
    itemP->expiresAt = ldIsoToNanoseconds(expiresP->value.s);

  // The write paths pass the format already parsed (and value-checked) by
  // ldCheckSubscription, so the string is matched only once. The cache-reload
  // path passes LdFormatUnset and we derive it from the stored tree: an absent
  // 'format' is the normalized default (ldFormatFromString(NULL) == LdFormatNone),
  // but a 'format' that is present and not a string is a corrupt stored document.
  if (format != LdFormatUnset)
    itemP->format = format;
  else
  {
    KjNode* formatP = kjLookup(notifP, LD_VOCAB_FORMAT);
    if (formatP != NULL && formatP->type != KjString)
      KT_RE(NULL, "Corrupted DB: subscription '%s' has a non-string 'notification.format' — refusing to cache it",
            itemP->subId ? itemP->subId : "?");
    // A subscription notification format is always an entity representation,
    // never a temporal one — temporal=false.
    itemP->format = ldFormatFromString((formatP != NULL) ? formatP->value.s : NULL, /*temporal*/false);
  }

  if (notifP != NULL)
  {
    KjNode* sysP  = kjLookup(notifP, "sysAttrs");
    KjNode* showP = kjLookup(notifP, "showChanges");
    itemP->sysAttrs    = (sysP  != NULL && sysP->type  == KjBoolean && sysP->value.b  == true);
    itemP->showChanges = (showP != NULL && showP->type == KjBoolean && showP->value.b == true);
  }

  // User-provided `jsonldContext` (the spec-visible field) wins; otherwise a
  // receiverInfo @context Link supplies the notification @context; otherwise
  // fall back to `_jcResolved`, the broker-filled internal URL written by
  // postSubscriptions when the user didn't supply one. (Precedence:
  // jsonldContext > receiverInfo Link > _jcResolved.)
  KjNode* jcP        = kjLookup(itemP->subTree, "jsonldContext");
  bool    explicitJc = (jcP != NULL && jcP->type == KjString);
  if (!explicitJc)
    jcP = kjLookup(itemP->subTree, "_jcResolved");
  itemP->contextUrl = (jcP != NULL && jcP->type == KjString) ? jcP->value.s : NULL;

  if (!explicitJc)
  {
    const char* riLink = riHeaderValue(itemP->receiverInfo, "Link");
    if (riLink != NULL && strstr(riLink, "json-ld#context") != NULL)
    {
      // Extract the URL between '<' and '>'. The receiverInfo Link is then NOT
      // re-emitted as a header — the broker emits this single @context Link.
      const char* lt = strchr(riLink, '<');
      const char* gt = (lt != NULL) ? strchr(lt, '>') : NULL;
      if (lt != NULL && gt != NULL && gt > lt + 1)
      {
        int   len = (int) (gt - (lt + 1));
        char* url = (char*) kaAlloc(&cacheP->alloc, len + 1);
        if (url != NULL)
        {
          memcpy(url, lt + 1, len);
          url[len] = 0;
          itemP->contextUrl = url;
        }
      }
    }
  }

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

    // kjChildRemove only unlinks; subTree is a malloc clone (kjClone), so free
    // each stripped stat node or it leaks (definite-lost on reload of a sub that
    // had persisted stats — i.e. was flushed before a restart).
    if (tsP != NULL) { kjChildRemove(notifP, tsP); kjFree(tsP); }
    if (tfP != NULL) { kjChildRemove(notifP, tfP); kjFree(tfP); }
    if (lnP != NULL) { kjChildRemove(notifP, lnP); kjFree(lnP); }
    if (lsP != NULL) { kjChildRemove(notifP, lsP); kjFree(lsP); }
    if (lfP != NULL) { kjChildRemove(notifP, lfP); kjFree(lfP); }
  }

  //
  // § 5.8.1.4 — distributed-subscription mapping. Persisted as
  //   _subordinates: [ {"id": <remoteSubId>, "regId": <regId>, "runNo": N}, ... ]
  //   _subordinateRunNo: N
  // alongside the rest of the sub doc. Read into cache fields and strip
  // from subTree so the GET path doesn't echo them.
  //
  KjNode* subListP = kjLookup(itemP->subTree, "_subordinates");
  if (subListP != NULL)
  {
    if (subListP->type == KjArray)
    {
      LdSubSubordinate* tail = NULL;
      for (KjNode* entryP = subListP->value.firstChildP; entryP != NULL; entryP = entryP->next)
      {
        if (entryP->type != KjObject) continue;

        KjNode* idP    = kjLookup(entryP, "id");
        KjNode* regIdP = kjLookup(entryP, "regId");
        KjNode* runP   = kjLookup(entryP, "runNo");

        if (idP    == NULL || idP->type    != KjString) continue;
        if (regIdP == NULL || regIdP->type != KjString) continue;

        LdSubSubordinate* node = (LdSubSubordinate*) calloc(1, sizeof(LdSubSubordinate));
        node->remoteSubId = strdup(idP->value.s);
        node->regId       = strdup(regIdP->value.s);
        node->runNo       = (runP != NULL && runP->type == KjInt) ? (int) runP->value.i : 0;
        node->next        = NULL;

        if (tail == NULL) itemP->subordinateP = node;
        else              tail->next = node;
        tail = node;
      }
    }
    // kjChildRemove only unlinks; subTree is a malloc clone, so free the
    // stripped subordinate-list subtree or it is orphaned (definite-lost on
    // every add/reload of a sub that carried persisted subordinate data).
    kjChildRemove(itemP->subTree, subListP);
    kjFree(subListP);
  }

  KjNode* runNoP = kjLookup(itemP->subTree, "_subordinateRunNo");
  if (runNoP != NULL)
  {
    if (runNoP->type == KjInt)
      itemP->subordinateRunNo = (int) runNoP->value.i;
    kjChildRemove(itemP->subTree, runNoP);
    kjFree(runNoP);
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

  ldSubCacheSubordinatesFree(itemP->subordinateP);

  // § 5.2.x throttling — drop any still-buffered (un-flushed) dirty entries.
  ldThrottleDirtyFree(itemP);
  pthread_mutex_destroy(&itemP->dirtyLock);

  // qExpr, scopeExpr, geoRel were malloc'd by parsers — need recursive free
  // For now, accept the leak; these are small and the cache lives for the
  // broker's lifetime. A proper free would need ldQFree, ldScopeExprFree, etc.

  free(itemP);
}



// -----------------------------------------------------------------------------
//
// ldSubCacheItemRemove -
//
// Caller MUST hold the wrlock. Unlinks + frees the item — UNLESS a reader still
// has it pinned, in which case it is parked on retiredList (reaped later).
bool ldSubCacheItemRemove(LdSubCache* cacheP, const char* subId)
{
  if (cacheP == NULL || subId == NULL)
    return false;

  cacheReapRetired(cacheP);

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

  itemP = cacheP->retiredList;
  while (itemP != NULL)
  {
    LdSubCacheItem* nextP = itemP->next;
    cacheItemFree(itemP);
    itemP = nextP;
  }

  pthread_rwlock_destroy(&cacheP->lock);
  free(cacheP);
}
