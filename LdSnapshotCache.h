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
#include <pthread.h>                                   // pthread_rwlock_t
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // uint64_t

#include "corTree/CorNode.h"                             // CorNode



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



// -----------------------------------------------------------------------------
//
// LdSnapshotCacheItem - one snapshot in the cache.
//
typedef struct LdSnapshotCacheItem
{
  char*                         id;             // URI; either client-supplied or auto-generated
  CorNode*                      tree;           // canonical Snapshot doc (all-malloc clone; freed on delete)
  void*                         snapTenantP;    // Tenant* — the snapshot's own DB tenant (entity store)
  int                           snapSeq;        // monotonic per-tenant sequence; suffixes the snap-tenant name
  LdSnapshotStatus              status;
  uint64_t                      createdAt;      // ns since epoch
  uint64_t                      modifiedAt;     // ns since epoch
  uint64_t                      expiresAt;      // ns since epoch
  uint64_t                      lastUsedAt;     // ns since epoch — touched on each read
  int                           priority;       // 1..10, default 5

  //
  // References: one held by the cache while the item is in it, one per PIN. A reader that uses
  // the item past the cache lock - the capture worker across its DB work, a read routed to the
  // snapshot's tenant, a clone - pins it. DELETE unlinks the item under the wrlock and then
  // drops the cache's reference; whoever takes the count to 0 destroys the item: the destroy
  // hook (the snapshot's tenant and databases), then the tree and the item. Two DELETEs can't
  // both find it linked, so it is destroyed once.
  //
  int                           refCount;

  struct LdSnapshotCacheItem*   next;
} LdSnapshotCacheItem;



// -----------------------------------------------------------------------------
//
// LdSnapshotCache -
//
typedef struct LdSnapshotCache
{
  pthread_rwlock_t      lock;            // walks, lookups, tree CLONES: rd - add, unlink, tree MUTATION: wr
  LdSnapshotCacheItem*  head;
  int                   count;
  int                   nextSnapSeq;     // assigned to itemP->snapSeq on add; bumped at boot reload to max+1
} LdSnapshotCache;



// -----------------------------------------------------------------------------
//
// API
//
extern LdSnapshotCache*      ldSnapshotCacheCreate(void);

//
// Locking: ldSnapshotCacheItemAdd and ldSnapshotCacheItemDelete lock inside. A walk of head,
// ldSnapshotCacheItemLookup and a CLONE of an item's tree need the caller's rdlock; a MUTATION
// of an item's tree (or status) the caller's wrlock. An item used after the lock is released
// must be pinned - ldSnapshotCacheItemLookupPinned - and unpinned when done.
//
extern void                  ldSnapshotCacheRdLock(LdSnapshotCache* cacheP);
extern void                  ldSnapshotCacheWrLock(LdSnapshotCache* cacheP);
extern void                  ldSnapshotCacheUnlock(LdSnapshotCache* cacheP);
extern void                  ldSnapshotCacheItemPin(LdSnapshotCacheItem* itemP);
extern void                  ldSnapshotCacheItemUnpin(LdSnapshotCacheItem* itemP);   // may destroy it

// The broker's part of destroying an item (its snapshot tenant: TRoE drop, DB drop, free) -
// called once, by whoever releases the last reference, with no cache lock held. Set at startup.
typedef void (*LdSnapshotItemDestroyFn)(LdSnapshotCacheItem* itemP);
extern void                  ldSnapshotCacheDestroyHookSet(LdSnapshotItemDestroyFn fn);

// Returns the new item PINNED (the caller unpins), NULL if the id is taken - checked and added
// under one wrlock
extern LdSnapshotCacheItem*  ldSnapshotCacheItemAdd(LdSnapshotCache*  cacheP,
                                                    CorNode*          snapshotTree);

extern LdSnapshotCacheItem*  ldSnapshotCacheItemLookup(LdSnapshotCache* cacheP,
                                                       const char*      id);

extern LdSnapshotCacheItem*  ldSnapshotCacheItemLookupPinned(LdSnapshotCache* cacheP,
                                                             const char*      id);

// ldSnapshotRequestRelease - unpin the request's corNgsild.snapshotPinned, if any
extern void                  ldSnapshotRequestRelease(void);

// Unlinks the item and drops the cache's reference: destroyed now, or at its last unpin
extern bool                  ldSnapshotCacheItemDelete(LdSnapshotCache* cacheP,
                                                       const char*      id);

#endif  // CORNGSILD_LDSNAPSHOTCACHE_H_
