#ifndef SWNGSILD_LDREGCACHE_OPS_H_
#define SWNGSILD_LDREGCACHE_OPS_H_

//
// FILE            ldRegCache.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Context Source Registration cache operations.
//
#include <stdbool.h>                                   // bool

#include "swNgsild/LdRegCache.h"                       // LdRegCache, LdRegCacheItem



// -----------------------------------------------------------------------------
//
// ldRegCacheCreate - allocate an empty per-tenant registration cache
//
extern LdRegCache* ldRegCacheCreate(void);



// -----------------------------------------------------------------------------
//
// ldRegCacheItemAdd - parse a registration tree and add it to the cache
//
// regTree is kjClone'd internally (malloc allocator) — caller keeps ownership
// of the original.
//
extern LdRegCacheItem* ldRegCacheItemAdd(LdRegCache* cacheP, KjNode* regTree);



// -----------------------------------------------------------------------------
//
// ldRegCacheItemLookup - find a cached registration by ID
//
extern LdRegCacheItem* ldRegCacheItemLookup(LdRegCache* cacheP, const char* regId);



// -----------------------------------------------------------------------------
//
// ldRegCacheItemRemove - remove a registration from the cache by ID
//
extern bool ldRegCacheItemRemove(LdRegCache* cacheP, const char* regId);



// -----------------------------------------------------------------------------
//
// ldRegCacheRelease - free the entire cache and all items
//
extern void ldRegCacheRelease(LdRegCache* cacheP);

#endif  // SWNGSILD_LDREGCACHE_OPS_H_
