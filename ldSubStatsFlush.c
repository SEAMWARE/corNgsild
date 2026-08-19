//
// FILE            ldSubStatsFlush.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stddef.h>                                    // NULL

#include "corNgsild/LdSubCache.h"                       // LdSubCache, LdSubCacheItem
#include "corNgsild/LdPernotCache.h"                    // LdPernotCache, LdPernotItem
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

  int touched = 0;

  for (LdSubCacheItem* itemP = cacheP->itemList; itemP != NULL; itemP = itemP->next)
  {
    int deltaSent   = itemP->timesSent   - itemP->lastFlushedSent;
    int deltaFailed = itemP->timesFailed - itemP->lastFlushedFailed;

    if (deltaSent == 0 && deltaFailed == 0)
      continue;                        // nothing moved for this item

    // Snapshot the values we intend to persist — the cache may still be
    // receiving updates concurrently. If the flush succeeds, we move the
    // watermarks forward to these snapshots; anything that lands between
    // now and the next flush shows up as fresh delta next time.
    int      snapSent   = itemP->timesSent;
    int      snapFailed = itemP->timesFailed;
    uint64_t snapLastN  = itemP->lastNotification;
    uint64_t snapLastS  = itemP->lastSuccess;
    uint64_t snapLastF  = itemP->lastFailure;

    int rc = flushFn(tenantP, itemP->subId,
                     deltaSent, deltaFailed,
                     snapLastN, snapLastS, snapLastF);
    if (rc == 0)
    {
      itemP->lastFlushedSent   = snapSent;
      itemP->lastFlushedFailed = snapFailed;
      touched++;
    }
    // On failure, leave watermarks alone so the next flush retries the
    // same delta. Worst case: double-flushed counters on a crash between
    // flushFn returning and us updating watermarks — the $inc semantics
    // in the storage layer still compose correctly.
  }

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

  int touched = 0;

  for (LdPernotItem* itemP = cacheP->head; itemP != NULL; itemP = itemP->next)
  {
    int deltaSent   = itemP->timesSent   - itemP->lastFlushedSent;
    int deltaFailed = itemP->timesFailed - itemP->lastFlushedFailed;

    if (deltaSent == 0 && deltaFailed == 0)
      continue;

    int      snapSent   = itemP->timesSent;
    int      snapFailed = itemP->timesFailed;
    uint64_t snapLastN  = itemP->lastNotification;
    uint64_t snapLastS  = itemP->lastSuccess;
    uint64_t snapLastF  = itemP->lastFailure;

    int rc = flushFn(tenantP, itemP->subId,
                     deltaSent, deltaFailed,
                     snapLastN, snapLastS, snapLastF);
    if (rc == 0)
    {
      itemP->lastFlushedSent   = snapSent;
      itemP->lastFlushedFailed = snapFailed;
      touched++;
    }
  }

  return touched;
}
