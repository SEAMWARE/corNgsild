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



// -----------------------------------------------------------------------------
//
// ldRegCacheMatchForRetrieve - find CSRs that match a single-entity retrieve
//
// Walks cacheP->itemList, applies the § 5.12 matching algorithm against
// each item filtered by mode (caller picks exclusive / redirect /
// inclusive / auxiliary in turn so the dispatcher can sequence the
// passes per § 5.6 / § 5.7).
//
// Match conditions per RegistrationInfo:
//   - The EntityInfo's type matches entityType (or entityType is NULL,
//     meaning "we don't know the type — accept any match"); AND
//   - Either the EntityInfo has neither id nor idPattern, OR
//     entityId == EntityInfo.id, OR
//     entityId matches EntityInfo.idPattern.
//
// On match the LdRegCacheItem* is written into *matchVP[i]. The array
// is malloc'd; caller must free(*matchVP) (the items themselves are
// owned by the cache — do not free them).
//
// Returns the count of matches (0 = no matches; *matchVP unchanged).
//
// Attribute-set narrowing (RegistrationInfo.propertyNames /
// relationshipNames) is NOT applied here yet — the simplest exclusive
// retrieveEntity case doesn't need it. Will be added when the
// dispatcher needs to choose a subset of attrs to forward.
//
extern int ldRegCacheMatchForRetrieve(LdRegCache*       cacheP,
                                       const char*       entityId,
                                       const char*       entityType,
                                       LdRegMode         modeFilter,
                                       LdRegCacheItem*** matchVP);

#endif  // SWNGSILD_LDREGCACHE_OPS_H_
