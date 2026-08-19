#ifndef COR_NGSILD_LD_STATS_FLUSH_LOOP_H
#define COR_NGSILD_LD_STATS_FLUSH_LOOP_H

//
// FILE            ldStatsFlushLoop.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Background pthread that periodically flushes subscription stats
// (entity-sub + CSR-sub + pernot) to the current-state DB plugin.
//
// The actual work — walking every tenant's three caches and calling
// the storage flush callback — is broker-side (lives next to the
// DbDriver). This loop only owns the scheduling: "fire the broker's
// callback every N seconds until ldStatsFlushLoopStop is called."
//

typedef void (*LdStatsFlushAllFn)(void);



// -----------------------------------------------------------------------------
//
// ldStatsFlushLoopStart - start the periodic flush thread
//
// $1: intervalSec — flush cadence in seconds. 0 disables the timer
//     entirely (the admin endpoint still works).
// $2: flushFn     — the broker-supplied all-caches walker.
//
// Idempotent. Calling twice is a no-op on the second call.
//
extern void ldStatsFlushLoopStart(int intervalSec, LdStatsFlushAllFn flushFn);



// -----------------------------------------------------------------------------
//
// ldStatsFlushLoopStop - signal the thread to exit
//
// Does not block. The thread completes its current cycle and exits
// at the next interval boundary.
//
extern void ldStatsFlushLoopStop(void);

#endif  // COR_NGSILD_LD_STATS_FLUSH_LOOP_H
