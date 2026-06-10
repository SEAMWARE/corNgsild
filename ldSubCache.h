#ifndef SWNGSILD_LDSUBCACHE_OPS_H_
#define SWNGSILD_LDSUBCACHE_OPS_H_

//
// FILE            ldSubCache.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Subscription cache operations.
//
#include "swNgsild/LdSubCache.h"                      // LdSubCache, LdSubCacheItem



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
// subTree is kjClone'd internally (malloc allocator) — the caller keeps ownership
// of the original.
// qExpr:  pre-parsed q-filter tree (NULL if no q). Ownership transferred to cache.
//         If NULL and the subscription has a q field, it will NOT be re-parsed
//         (the expanded IRI format is not parseable by ldQParse).
//
extern LdSubCacheItem* ldSubCacheItemAdd(LdSubCache* cacheP, KjNode* subTree, LdQNode* qExpr);



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
// ldSubCacheRelease - free the entire cache and all items
//
extern void ldSubCacheRelease(LdSubCache* cacheP);

#endif  // SWNGSILD_LDSUBCACHE_OPS_H_
