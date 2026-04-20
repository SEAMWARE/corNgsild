#ifndef SWNGSILD_LDENTITYMAP_OPS_H_
#define SWNGSILD_LDENTITYMAP_OPS_H_

//
// FILE            ldEntityMap.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include "kjson/KjNode.h"                              // KjNode
#include "swNgsild/LdEntityMap.h"                      // LdEntityMap, LdEntityMapStore



// ldEntityMapStoreCreate - allocate an empty store
extern LdEntityMapStore* ldEntityMapStoreCreate(void);

// ldEntityMapCreate - create a new entity map with a generated ID and expiry
extern LdEntityMap* ldEntityMapCreate(LdEntityMapStore* storeP, uint64_t lifetimeNs, void* tenantP);

// ldEntityMapAddEntry - add an entity ID → sources mapping
extern void ldEntityMapAddEntry(LdEntityMap* mapP, const char* entityId,
                                 const char** sourceIdV, int sourceCount);

// ldEntityMapLookup - find a map by ID
extern LdEntityMap* ldEntityMapLookup(LdEntityMapStore* storeP, const char* mapId);

// ldEntityMapRemove - remove and free a map by ID
extern bool ldEntityMapRemove(LdEntityMapStore* storeP, const char* mapId);

// ldEntityMapSetExpiresAt - overwrite a map's expiresAt (§ 5.14.2)
extern void ldEntityMapSetExpiresAt(LdEntityMap* mapP, uint64_t expiresAtNs);

// ldEntityMapToTree - render an EntityMap as a KjNode tree for API output
extern KjNode* ldEntityMapToTree(LdEntityMap* mapP);

// ldEntityMapPurgeExpired - remove all expired maps from the store
extern void ldEntityMapPurgeExpired(LdEntityMapStore* storeP);

#endif  // SWNGSILD_LDENTITYMAP_OPS_H_
