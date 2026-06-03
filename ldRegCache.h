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
#include <stdint.h>                                    // uint64_t
#include <regex.h>                                     // regex_t

#include "swNgsild/LdOp.h"                             // LdOp
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
                                       char**            entityTypeV,
                                       LdRegMode         modeFilter,
                                       LdRegCacheItem*** matchVP);



// -----------------------------------------------------------------------------
//
// ldRegCacheMatchForRetrieveScoped - like ldRegCacheMatchForRetrieve but
// also filters by the entity's scope (§ 5.2.9 csr.scope vs entity scope).
// entityScopeV is a NULL-terminated string array of the entity's scope
// values; pass NULL to skip scope filtering (= same behaviour as the
// non-scoped variant).
//
extern int ldRegCacheMatchForRetrieveScoped(LdRegCache*       cacheP,
                                             const char*       entityId,
                                             char**            entityTypeV,
                                             char**            entityScopeV,
                                             LdRegMode         modeFilter,
                                             LdRegCacheItem*** matchVP);



// -----------------------------------------------------------------------------
//
// ldRegCacheMatchForDiscovery - § 5.10.2 Query Context Source Registrations.
//
// Walks every non-expired CSR in the cache regardless of mode and returns
// those with at least one RegistrationInfo matching the filters. Caller
// frees *matchVP with free().
//
// typeV:      NULL = ignore; else NULL-terminated expanded IRIs
// entityIdV:  NULL or empty = ignore; else NULL-terminated list of exact
//             URIs (OR — match any). Maps to ?id=A,B (§ 6.4.3.2 comma list).
// idPattern:  NULL = ignore; else POSIX regex matched request-side against
//             each EntityInfo.id
// attrsV:     NULL = ignore; else NULL-terminated expanded attribute IRIs
//             matched against RegistrationInfo.propertyNames /
//             relationshipNames (§ 5.12). `attrs` and `pick` in the HTTP
//             binding both map to this parameter.
//
extern int ldRegCacheMatchForDiscovery(LdRegCache*       cacheP,
                                       char**            typeV,
                                       char**            entityIdV,
                                       const char*       idPattern,
                                       char**            attrsV,
                                       LdRegCacheItem*** matchVP);



// -----------------------------------------------------------------------------
//
// ldRegInfoDiscoveryMatches - § 5.12 match of a single RegistrationInfo
// against the discovery filters: AND across (type, entityIdV, idRegex,
// attrsV). entityIdV is OR-of (any of). Used by getCsourceRegistrations
// to filter the response's information[] array (§ 5.10.2.5) so only
// matching entries are returned.
//
extern bool ldRegInfoDiscoveryMatches(LdRegInfo*     riP,
                                      char**         entityIdV,
                                      char**         typeV,
                                      const regex_t* idRegex,
                                      char**         attrsV);



// -----------------------------------------------------------------------------
//
// ldRegOpSupported - true if a registration declares support for opBit
//
// opBit is a single LdOp bit (e.g. LdOpCreateEntity), typically the
// matched service's own ldOp (swRest.serviceP->ldOp). Inlined to a
// single AND — operationsMask already carries the § 4.20 default
// (federationOps) when the registration omits operations[].
//
static inline bool ldRegOpSupported(const LdRegCacheItem* itemP, LdOp opBit)
{
  return (itemP->operationsMask & (uint64_t) opBit) != 0;
}

// -----------------------------------------------------------------------------
//
// ldRegCacheLocalWriteConflict - § 9.3.3 guard for ?local=true writes.
// Returns the regId of the first exclusive/redirect registration whose
// claim overlaps the written (entityId, entityTypeV, attrIriV), or NULL.
//
extern const char* ldRegCacheLocalWriteConflict(LdRegCache* cacheP,
                                                const char* entityId,
                                                char**      entityTypeV,
                                                char**      entityScopeV,
                                                char**      attrIriV);

#endif  // SWNGSILD_LDREGCACHE_OPS_H_
