#ifndef CORNGSILD_LDSUBCACHE_OPS_H_
#define CORNGSILD_LDSUBCACHE_OPS_H_

//
// FILE            ldSubCache.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Subscription cache operations.
//
#include "corAlloc/CorAlloc.h"                          // CorAlloc
#include "corTree/CorNode.h"                            // CorNode
#include "corNgsild/LdSubCache.h"                      // LdSubCache, LdSubCacheItem



// -----------------------------------------------------------------------------
//
// ldSubCacheCreate - allocate an empty per-tenant subscription cache
//
extern LdSubCache* ldSubCacheCreate(void);



// -----------------------------------------------------------------------------
//
// Concurrency (mirror of the reg cache). Readers (notify drain, CSR-sub match)
// rdlock the walk and pin items used across a notification send; writers (sub
// CRUD) wrlock the list mutation. LOCK ORDER: reg lock BEFORE sub lock — a sub
// writer that also touches the reg cache must pin + release the sub lock first.
// ldSubCacheItemLookup stays a pure caller-holds-lock primitive. NULL-safe.
//
extern void ldSubCacheRdLock(LdSubCache* cacheP);
extern void ldSubCacheWrLock(LdSubCache* cacheP);
extern void ldSubCacheUnlock(LdSubCache* cacheP);
extern void ldSubCacheItemPin(LdSubCacheItem* itemP);
extern void ldSubCacheItemUnpin(LdSubCacheItem* itemP);



// -----------------------------------------------------------------------------
//
// ldSubCacheItemAdd - parse a subscription tree and add it to the cache
//
// subTree is corTreeClone'd internally (malloc allocator) — the caller keeps ownership
// of the original.
// qExpr:  pre-parsed q-filter tree (NULL if no q). Ownership transferred to cache.
//         If NULL and the subscription has a q field, it will NOT be re-parsed
//         (the expanded IRI format is not parseable by ldQParse).
// format: the notification format, already parsed by the caller (the write paths
//         get it from ldCheckSubscription, so the string isn't matched twice).
//         Pass LdFormatUnset to have it derived from the subTree (cache-reload path).
//
extern LdSubCacheItem* ldSubCacheItemAdd(LdSubCache* cacheP, CorNode* subTree, LdQNode* qExpr, LdFormat format);



// -----------------------------------------------------------------------------
//
// ldSubCacheItemLookup - find a cached subscription by ID
//
extern LdSubCacheItem* ldSubCacheItemLookup(LdSubCache* cacheP, const char* subId);



// -----------------------------------------------------------------------------
//
// ldSubCacheItemRemove - remove a subscription from the cache by ID
//
extern bool ldSubCacheItemRemove(LdSubCache* cacheP, const char* subId);



// -----------------------------------------------------------------------------
//
// ldSubCacheEmpty - does the cache hold no subscription at all?
//
// Then the matching at the end of the request cannot find one: queuing a write for it (ldNotifyDefer) is
// work for nothing - and it is what tells the HTTP layer that the request still has something to do
// after the response (the built-in server's post-response phase goes to a worker thread for it) - and so
// is reading the entity back after a write only to queue it. Under the cache's read lock, as the matching
// itself is: a subscription created while the write is in flight is matched or not exactly as it would
// have been by the matching.
//
extern bool ldSubCacheEmpty(LdSubCache* cacheP);



// -----------------------------------------------------------------------------
//
// ldSubCacheCandidates - the subscriptions that can match an entity, by its id and its types
//
// Every subscription the full match (trigger, entities, watched attributes, q, ...) could accept is
// among them - those under the entity's id, under one of its types, and those always a candidate - in
// the list's order (of addition), each once. *vPP in kaP. The caller holds the (read) lock.
//
// typeP: the entity's "type" as the store holds it (expanded, string or array). entityId NULL: all.
//
extern int ldSubCacheCandidates(LdSubCache* cacheP, const char* entityId, CorNode* typeP, CorAlloc* kaP, LdSubCacheItem*** vPP);



// -----------------------------------------------------------------------------
//
// ldSubCacheRelease - free the entire cache and all items
//
extern void ldSubCacheRelease(LdSubCache* cacheP);



// -----------------------------------------------------------------------------
//
// ldSubCacheSubordinatesFree - free a derived-sub (subordinate) mapping list
//
extern void ldSubCacheSubordinatesFree(LdSubSubordinate* head);

#endif  // CORNGSILD_LDSUBCACHE_OPS_H_
