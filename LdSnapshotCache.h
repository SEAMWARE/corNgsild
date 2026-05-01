#ifndef SWNGSILD_LDSNAPSHOTCACHE_H_
#define SWNGSILD_LDSNAPSHOTCACHE_H_

//
// FILE            LdSnapshotCache.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// In-memory cache of NGSI-LD Snapshots (§ 5.16, § 5.2.41).
//
// Phase 1: bare CRUD store. Each snapshot is held as a deep-cloned
// KjNode tree owned by the cache's allocator, plus a couple of
// hot-access fields broken out for fast lookup. No async query
// execution, no entity storage tied to the snapshot, no
// snapshot-aware querying yet. Those land in later phases.
//
// One cache per tenant, hung off Tenant::snapshotCacheP.
//
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // uint64_t

#include "kalloc/KAlloc.h"                               // KAlloc
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
  KjNode*                       tree;           // canonical Snapshot doc (clone owned by cache alloc)
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
  KAlloc                alloc;
  char                  allocBuf[16 * 1024];
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

#endif  // SWNGSILD_LDSNAPSHOTCACHE_H_
