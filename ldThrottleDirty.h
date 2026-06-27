#ifndef SWNGSILD_LDTHROTTLEDIRTY_H_
#define SWNGSILD_LDTHROTTLEDIRTY_H_

//
// FILE            ldThrottleDirty.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// § 5.2.x throttling — the coalesce-to-latest dirty set of a throttled
// subscription. A match landing inside the per-subscription throttling window
// is BUFFERED here (deduped by entity id) instead of dropped; the periodic
// flush re-queries the latest state and sends one coalesced notification when
// the window elapses. See LdThrottleEntry / the dirty* fields in LdSubCache.h.
//
#include <stdint.h>                                    // uint64_t

#include "kjson/KjNode.h"                              // KjNode
#include "swNgsild/LdSubCache.h"                       // LdSubCacheItem, LdThrottleEntry



// -----------------------------------------------------------------------------
//
// ldThrottleDirtyUpsert - buffer (or coalesce) one matched entity
//
// Deduped by entityId under itemP->dirtyLock: an existing entry has its
// reasonsMask OR'd; a DELETE op wins over an update (and its final state is
// captured, since a deleted entity can't be re-queried at flush). entityP is
// only cloned for a delete. O(dirtyN) per call (linear dedup) — the buffer is
// bounded by matching-entity cardinality, not update rate.
//
extern void ldThrottleDirtyUpsert(LdSubCacheItem* itemP,
                                  const char*     entityId,
                                  int             reasonsMask,
                                  int             op,
                                  uint64_t        deletedAtNs,
                                  KjNode*         entityP);



// -----------------------------------------------------------------------------
//
// ldThrottleDirtyDrain - hand the whole dirty set to the caller and reset it
//
// Moves ownership of the entry array out of the item (under dirtyLock) and
// leaves the item's dirty set empty. The caller must free the returned array
// with ldThrottleDirtyEntriesFree once the coalesced notification is sent.
//
extern void ldThrottleDirtyDrain(LdSubCacheItem* itemP, LdThrottleEntry** outV, int* outN);



// -----------------------------------------------------------------------------
//
// ldThrottleDirtyEntriesFree - free a drained entry array (ids + delete clones)
//
extern void ldThrottleDirtyEntriesFree(LdThrottleEntry* v, int n);



// -----------------------------------------------------------------------------
//
// ldThrottleDirtyFree - free the item's current dirty set (on sub delete)
//
// Caller holds the cache wrlock and the item is being freed, so no dirtyLock is
// taken. Frees the entries + array but does NOT destroy the mutex (cacheItemFree
// does that).
//
extern void ldThrottleDirtyFree(LdSubCacheItem* itemP);

#endif  // SWNGSILD_LDTHROTTLEDIRTY_H_
