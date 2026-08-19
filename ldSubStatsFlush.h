#ifndef COR_NGSILD_LD_SUB_STATS_FLUSH_H
#define COR_NGSILD_LD_SUB_STATS_FLUSH_H

//
// FILE            ldSubStatsFlush.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Subscription-stats flush: walks the entity-sub + CSR-sub caches of every
// tenant, computes per-item deltas (timesSent/timesFailed since last flush)
// and hands them to the DB-driver-supplied flush callback. Last-* timestamps
// are sent through whenever they moved since the last flush.
//
// The flush callback signature matches DbSubscriptionStatsFlushFunc — any
// storage plugin that supports HA-safe increment can wire it up; plugins
// that can't (e.g. ramdb) can pass NULL and the walker becomes a no-op.
//
#include <stdint.h>                                   // uint64_t

#include "corNgsild/LdSubCache.h"                      // LdSubCache
#include "corNgsild/LdPernotCache.h"                   // LdPernotCache



typedef int (*LdSubStatsFlushFn)(void*        tenantP,
                                 const char*  subId,
                                 int          deltaSent,
                                 int          deltaFailed,
                                 uint64_t     lastNotification,
                                 uint64_t     lastSuccess,
                                 uint64_t     lastFailure);



// -----------------------------------------------------------------------------
//
// ldSubStatsFlush - flush delta counters for every item in one sub cache
//
// $1: tenantP (opaque to corNgsild — passed through to flushFn)
// $2: cacheP — entity-sub or CSR-sub cache
// $3: flushFn — storage callback (may be NULL → walker is a no-op)
//
// Returns the number of items that had non-zero deltas (regardless of
// flushFn result). -1 if the arguments were bad.
//
extern int ldSubStatsFlush(void*              tenantP,
                           LdSubCache*        cacheP,
                           LdSubStatsFlushFn  flushFn);



// -----------------------------------------------------------------------------
//
// ldPernotStatsFlush - flush delta counters for every item in one pernot cache
//
// Same semantics as ldSubStatsFlush but for LdPernotCache. Pernots share
// the `subscriptions` collection with entity + CSR subs (tagged by the
// presence of timeInterval), so the storage callback signature and the
// on-disk field paths are identical.
//
extern int ldPernotStatsFlush(void*              tenantP,
                              LdPernotCache*     cacheP,
                              LdSubStatsFlushFn  flushFn);

#endif  // COR_NGSILD_LD_SUB_STATS_FLUSH_H
