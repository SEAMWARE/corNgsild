#ifndef CORNGSILD_LDSNAPSHOTCACHE_H_
#define CORNGSILD_LDSNAPSHOTCACHE_H_

//
// FILE            LdSnapshotCache.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// In-memory cache of NGSI-LD Snapshots (§ 5.16, § 5.2.41).
//
// The cache holds metadata only — the snapshot tree (id, status,
// query criteria, timestamps, priority) and a borrowed pointer to
// the snapshot's own tenant (`snapTenantP`). Frozen entity bodies
// live in the DB tenant identified by snapTenantP; reads route
// through it via the standard db.entityQuery / db.entityRetrieve
// path. This keeps the cache size bounded regardless of the
// snapshot's entity count (TB-scale captures stream straight to DB).
//
// One cache per tenant, hung off Tenant::snapshotCacheP.
//
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // uint64_t

#include "kjson/KjNode.h"                                // KjNode



// -----------------------------------------------------------------------------
//
// LdSnapshotStatus -
//
typedef enum LdSnapshotStatus
{
  LdSnapshotPreparing = 0,
  LdSnapshotSuccess,
  LdSnapshotPartial,
  LdSnapshotEmpty,
  LdSnapshotFailure
} LdSnapshotStatus;



extern const char* ldSnapshotStatusToString(LdSnapshotStatus s);



// -----------------------------------------------------------------------------
//
// LdSnapshotCacheItem - one snapshot in the cache.
//
typedef struct LdSnapshotCacheItem
{
  char*                         id;             // URI; either client-supplied or auto-generated
  KjNode*                       tree;           // canonical Snapshot doc (all-malloc clone; freed on delete)
  void*                         snapTenantP;    // Tenant* — the snapshot's own DB tenant (entity store)
  int                           snapSeq;        // monotonic per-tenant sequence; suffixes the snap-tenant name
  LdSnapshotStatus              status;
  uint64_t                      createdAt;      // ns since epoch
  uint64_t                      modifiedAt;     // ns since epoch
  uint64_t                      expiresAt;      // ns since epoch
  uint64_t                      lastUsedAt;     // ns since epoch — touched on each read
  int                           priority;       // 1..10, default 5
  struct LdSnapshotCacheItem*   next;
} LdSnapshotCacheItem;



// -----------------------------------------------------------------------------
//
// LdSnapshotCache -
//
typedef struct LdSnapshotCache
{
  LdSnapshotCacheItem*  head;
  int                   count;
  int                   nextSnapSeq;     // assigned to itemP->snapSeq on add; bumped at boot reload to max+1
} LdSnapshotCache;



// -----------------------------------------------------------------------------
//
// API
//
extern LdSnapshotCache*      ldSnapshotCacheCreate(void);

extern LdSnapshotCacheItem*  ldSnapshotCacheItemAdd(LdSnapshotCache*  cacheP,
                                                    KjNode*           snapshotTree);

extern LdSnapshotCacheItem*  ldSnapshotCacheItemLookup(LdSnapshotCache* cacheP,
                                                       const char*      id);

extern bool                  ldSnapshotCacheItemDelete(LdSnapshotCache* cacheP,
                                                       const char*      id);

#endif  // CORNGSILD_LDSNAPSHOTCACHE_H_
