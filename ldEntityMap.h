#ifndef CORNGSILD_LDENTITYMAP_OPS_H_
#define CORNGSILD_LDENTITYMAP_OPS_H_

//
// FILE            ldEntityMap.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include "corTree/CorNode.h"                           // CorNode
#include "corNgsild/LdEntityMap.h"                      // LdEntityMap, LdEntityMapStore



// ldEntityMapStoreCreate - allocate an empty store
extern LdEntityMapStore* ldEntityMapStoreCreate(void);

// ldEntityMapCreate - create a new entity map with a generated ID and expiry
// Locking (see LdEntityMap): ldEntityMapCreate / Remove / PurgeExpired lock inside; a walk of
// storeP->head or ldEntityMapLookup needs the caller's rdlock; a map used after the lock is
// released must be pinned (ldEntityMapLookupPinned) and unpinned when done.
extern void ldEntityMapStoreRdLock(LdEntityMapStore* storeP);
extern void ldEntityMapStoreUnlock(LdEntityMapStore* storeP);
extern void ldEntityMapPin(LdEntityMap* mapP);
extern void ldEntityMapUnpin(LdEntityMap* mapP);             // the last reference frees the map
extern LdEntityMap* ldEntityMapLookupPinned(LdEntityMapStore* storeP, const char* mapId);

// ldEntityMapRequestPin - hold mapP pinned until the request ends (corNgsild.entityMapPinned;
// ldEntityMapRequestRelease, from the post-response hook, unpins). Takes over a pin the caller has.
extern void ldEntityMapRequestPin(LdEntityMap* mapP);
extern void ldEntityMapRequestRelease(void);

// Returns the new map PINNED (the caller unpins, or hands the pin to ldEntityMapRequestPin)
extern LdEntityMap* ldEntityMapCreate(LdEntityMapStore* storeP, uint64_t lifetimeNs, void* tenantP);

// ldEntityMapSetFilters - record the query's filter URL params (§ 9 same-parameters
// binding). Each non-NULL value is duplicated onto the map; NULL = not present.
extern void ldEntityMapSetFilters(LdEntityMap* mapP, const char* type, const char* q,
                                  const char* scopeQ, const char* georel, const char* geometry,
                                  const char* coordinates, const char* geoproperty);

// ldEntityMapAddEntry - add an entity ID → sources mapping
extern void ldEntityMapAddEntry(LdEntityMap* mapP, const char* entityId,
                                 const char** sourceIdV, int sourceCount);

// ldEntityMapAddLinkedMap - add a (CSR id → remote EntityMap id) pair to linkedMaps (§ 5.14.4.4)
extern void ldEntityMapAddLinkedMap(LdEntityMap* mapP, const char* csrId, const char* remoteMapId);

// ldEntityMapLinkedMapLookup - find the remote EntityMap id stored for a CSR
// (returns NULL when not present)
extern const char* ldEntityMapLinkedMapLookup(LdEntityMap* mapP, const char* csrId);

// ldEntityMapLookup - find a map by ID
extern LdEntityMap* ldEntityMapLookup(LdEntityMapStore* storeP, const char* mapId);

// ldEntityMapRemove - remove and free a map by ID
extern bool ldEntityMapRemove(LdEntityMapStore* storeP, const char* mapId);

// ldEntityMapSetExpiresAt - overwrite a map's expiresAt (§ 5.14.2)
extern void ldEntityMapSetExpiresAt(LdEntityMap* mapP, uint64_t expiresAtNs);

// ldEntityMapToTree - render an EntityMap as a CorNode tree for API output
extern CorNode* ldEntityMapToTree(LdEntityMap* mapP);

// ldEntityMapPurgeExpired - remove all expired maps from the store
extern void ldEntityMapPurgeExpired(LdEntityMapStore* storeP);

// -----------------------------------------------------------------------------
//
// ldEntityMapSetStoredQ - the bound q, resolved as a subscription stores it (ldQRenderStored)
//
// What a later page that omits q re-applies (ldQParseStored): independent of that page's @context, and
// with the expandValues / jsonKeys / langProperties of the query that created the map.
//
extern void ldEntityMapSetStoredQ(LdEntityMap* mapP, const char* storedQ);



// -----------------------------------------------------------------------------
//
// ldEntityMapMaxBytes - the memory every EntityMap of the broker may hold together, in bytes
//
// Set by the broker at start (coraine: --entityMapMemory). 0: no cap - and no automatic maps (the
// broker only creates maps by itself within a cap). An automatic map that does not fit, after the
// expired maps and the automatic maps used longest ago have gone, is not kept; nor is a map a
// client asked for (the broker answers 403 TooManyResults for that one).
//
extern int64_t ldEntityMapMaxBytes;

// ldEntityMapBytesTotal - what all EntityMaps hold now (an estimate, malloc overhead included)
extern int64_t ldEntityMapBytesTotal(void);

// ldEntityMapAdmit - mapP is filled: true if it is kept, false if it does not fit (it is out of the store then)
extern bool ldEntityMapAdmit(LdEntityMapStore* storeP, LdEntityMap* mapP);

// ldEntityMapTouch - a page was served from mapP (an automatic map's lifetime slides)
extern void ldEntityMapTouch(LdEntityMap* mapP);

// ldEntityMapRequestHeader - the NGSILD-EntityMap request header into corNgsild.entityMapId (false: 400 raised)
extern bool ldEntityMapRequestHeader(void);

#endif  // CORNGSILD_LDENTITYMAP_OPS_H_
