#ifndef CORNGSILD_LDPERNOTCACHE_OPS_H_
#define CORNGSILD_LDPERNOTCACHE_OPS_H_

//
// FILE            ldPernotCache.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include "corTree/CorNode.h"                           // CorNode
#include "corAlloc/CorAlloc.h"                         // CorAlloc

#include "corNgsild/LdPernotCache.h"                    // LdPernotCache, LdPernotItem
#include "corNgsild/LdQ.h"                              // LdQNode



// ldPernotCacheCreate - allocate and initialize an empty pernot cache
extern LdPernotCache* ldPernotCacheCreate(void);

// Locking (as LdSubCache): the loop thread walks this cache while requests (and the HA
// thread) add and remove, so:
//   - ldPernotCacheItemAdd / ldPernotCacheItemRemove take the WRITE lock themselves
//   - a walk of cacheP->head, or ldPernotCacheItemLookup, needs the caller's READ lock
//   - an item used after the lock is released must be PINNED first (ldPernotCacheItemPin),
//     and unpinned when done; ldPernotCacheItemLookupPinned does lookup + pin in one call
//
extern void ldPernotCacheRdLock(LdPernotCache* cacheP);
extern void ldPernotCacheUnlock(LdPernotCache* cacheP);
extern void ldPernotCacheItemPin(LdPernotItem* itemP);
extern void ldPernotCacheItemUnpin(LdPernotItem* itemP);

// ldPernotCacheItemLookupPinned - find by subscription ID and pin it (NULL: not there);
// the caller unpins
extern LdPernotItem* ldPernotCacheItemLookupPinned(LdPernotCache* cacheP, const char* subId);

// ldPernotCacheItemAdd - add a subscription to the pernot cache (takes the wrlock)
// q, scopeQ and geoQ are parsed from subTree, into the item's own arena
extern LdPernotItem* ldPernotCacheItemAdd(LdPernotCache* cacheP, CorNode* subTree, void* tenantP);

// ldPernotCacheItemReplace - replace the item of subTree's id (PATCH): a new item from subTree,
// with the old one's notification history (counters, last notification/success/failure), in
// the old one's place. Takes the wrlock. false: no item of that id.
extern bool ldPernotCacheItemReplace(LdPernotCache* cacheP, CorNode* subTree, void* tenantP);

// ldPernotCacheItemLookup - find by subscription ID - caller holds the rdlock
extern LdPernotItem* ldPernotCacheItemLookup(LdPernotCache* cacheP, const char* subId);

// ldPernotCacheItemRemove - remove by subscription ID (takes the wrlock; a pinned item is
// freed at its last unpin's next writer)
extern bool ldPernotCacheItemRemove(LdPernotCache* cacheP, const char* subId);

// ldPernotCacheRelease - free everything
extern void ldPernotCacheRelease(LdPernotCache* cacheP);

#endif  // CORNGSILD_LDPERNOTCACHE_OPS_H_
