//
// FILE            ldSubStatsFlush.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stddef.h>                                    // NULL
#include <stdint.h>                                    // uint64_t
#include <stdlib.h>                                    // realloc, free

#include "corNgsild/LdSubCache.h"                       // LdSubCache, LdSubCacheItem
#include "corNgsild/ldSubCache.h"                       // ldSubCacheRdLock, ldSubCacheUnlock, ldSubCacheItemPin/Unpin
#include "corNgsild/LdPernotCache.h"                    // LdPernotCache, LdPernotItem
#include "corNgsild/ldPernotCache.h"                    // ldPernotCacheRdLock, ldPernotCacheItemPin/Unpin
#include "corNgsild/ldSubStatsFlush.h"                  // Own interface



// -----------------------------------------------------------------------------
//
// ldSubStatsFlush -
//
int ldSubStatsFlush(void*              tenantP,
                    LdSubCache*        cacheP,
                    LdSubStatsFlushFn  flushFn)
{
  if (cacheP == NULL)
    return -1;
  if (flushFn == NULL)
    return 0;                         // storage doesn't support flushing — no-op

  //
  // This runs on the stats thread (and from the admin API), concurrently with requests that
  // create and DELETE subscriptions. It walked the cache with no lock and called the storage
  // layer mid-walk, so a DELETE in between freed the item it was flushing. Holding the rdlock
  // across the storage calls would stall every subscription write for the whole flush, so:
  // under the rdlock, snapshot what moved and PIN those items; flush unlocked; move the
  // watermarks on the pinned items; unpin.
  //
  typedef struct { LdSubCacheItem* itemP; int sent; int failed; int dSent; int dFailed; uint64_t lastN; uint64_t lastS; uint64_t lastF; } Snap;

  int   snapN   = 0;
  int   snapCap = 0;
  Snap* snapV   = NULL;

  ldSubCacheRdLock(cacheP);
  for (LdSubCacheItem* itemP = cacheP->itemList; itemP != NULL; itemP = itemP->next)
  {
    int deltaSent   = itemP->timesSent   - itemP->lastFlushedSent;
    int deltaFailed = itemP->timesFailed - itemP->lastFlushedFailed;

    if (deltaSent == 0 && deltaFailed == 0)
      continue;                        // nothing moved for this item

    if (snapN == snapCap)
    {
      int   newCap = (snapCap == 0) ? 16 : 2 * snapCap;
      Snap* newV   = (Snap*) realloc(snapV, newCap * sizeof(Snap));

      if (newV == NULL)
        break;                         // the rest are flushed next time

      snapV   = newV;
      snapCap = newCap;
    }

    // Snapshot the values we intend to persist — the cache may still be
    // receiving updates concurrently. If the flush succeeds, we move the
    // watermarks forward to these snapshots; anything that lands between
    // now and the next flush shows up as fresh delta next time.
    ldSubCacheItemPin(itemP);
    snapV[snapN] = (Snap) { itemP, itemP->timesSent, itemP->timesFailed, deltaSent, deltaFailed,
                            itemP->lastNotification, itemP->lastSuccess, itemP->lastFailure };
    snapN++;
  }
  ldSubCacheUnlock(cacheP);

  int touched = 0;

  for (int i = 0; i < snapN; i++)
  {
    Snap* sP = &snapV[i];
    int   rc = flushFn(tenantP, sP->itemP->subId,
                       sP->dSent, sP->dFailed,
                       sP->lastN, sP->lastS, sP->lastF);
    if (rc == 0)
    {
      sP->itemP->lastFlushedSent   = sP->sent;
      sP->itemP->lastFlushedFailed = sP->failed;
      touched++;
    }
    // On failure, leave watermarks alone so the next flush retries the
    // same delta. Worst case: double-flushed counters on a crash between
    // flushFn returning and us updating watermarks — the $inc semantics
    // in the storage layer still compose correctly.

    ldSubCacheItemUnpin(sP->itemP);
  }

  free(snapV);

  return touched;
}



// -----------------------------------------------------------------------------
//
// ldPernotStatsFlush -
//
int ldPernotStatsFlush(void*              tenantP,
                       LdPernotCache*     cacheP,
                       LdSubStatsFlushFn  flushFn)
{
  if (cacheP == NULL)
    return -1;
  if (flushFn == NULL)
    return 0;

  //
  // As ldSubStatsFlush: snapshot + pin under the rdlock, flush (storage I/O) unlocked, unpin.
  // It walked the cache with no lock while subscriptions were deleted.
  //
  typedef struct { LdPernotItem* itemP; int sent; int failed; int dSent; int dFailed; uint64_t lastN; uint64_t lastS; uint64_t lastF; } Snap;

  int   snapN   = 0;
  int   snapCap = 0;
  Snap* snapV   = NULL;

  ldPernotCacheRdLock(cacheP);
  for (LdPernotItem* itemP = cacheP->head; itemP != NULL; itemP = itemP->next)
  {
    int deltaSent   = itemP->timesSent   - itemP->lastFlushedSent;
    int deltaFailed = itemP->timesFailed - itemP->lastFlushedFailed;

    if (deltaSent == 0 && deltaFailed == 0)
      continue;

    if (snapN == snapCap)
    {
      int   newCap = (snapCap == 0) ? 16 : 2 * snapCap;
      Snap* newV   = (Snap*) realloc(snapV, newCap * sizeof(Snap));

      if (newV == NULL)
        break;

      snapV   = newV;
      snapCap = newCap;
    }

    ldPernotCacheItemPin(itemP);
    snapV[snapN] = (Snap) { itemP, itemP->timesSent, itemP->timesFailed, deltaSent, deltaFailed,
                            itemP->lastNotification, itemP->lastSuccess, itemP->lastFailure };
    snapN++;
  }
  ldPernotCacheUnlock(cacheP);

  int touched = 0;

  for (int i = 0; i < snapN; i++)
  {
    Snap* sP = &snapV[i];
    int   rc = flushFn(tenantP, sP->itemP->subId,
                       sP->dSent, sP->dFailed,
                       sP->lastN, sP->lastS, sP->lastF);
    if (rc == 0)
    {
      sP->itemP->lastFlushedSent   = sP->sent;
      sP->itemP->lastFlushedFailed = sP->failed;
      touched++;
    }

    ldPernotCacheItemUnpin(sP->itemP);
  }

  free(snapV);

  return touched;
}
