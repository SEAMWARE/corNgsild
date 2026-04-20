//
// FILE            ldNotifyDefer.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef SWNGSILD_LDNOTIFYDEFER_H_
#define SWNGSILD_LDNOTIFYDEFER_H_

#include "kjson/KjNode.h"                        // KjNode
#include "swNgsild/ldSubCache.h"                 // LdSubCache
#include "swNgsild/ldSubscriptionNotify.h"       // LdNotifyOp, LdNotifyPendingEntry
#include "swNgsild/ldEntityMerge.h"              // LdMergeReport



// -----------------------------------------------------------------------------
//
// ldNotifyDefer - append one entity-change candidate to the thread-local queue
//
// Called by entity service routines (single-entity and batch). Each call
// appends an entry to a per-thread dynamic array. For a single-entity
// request that's one entry; a batch request appends one per merged
// instance, in order.
//
// The queue is drained post-response by ldNotifyDispatchPending() — wired
// from main via swRestSetPostResponseHook().
//
// entityP must stay valid until dispatch. Both the request arena and the
// tenant's entity store satisfy that; the referenced LdMergeReport is
// copied by value so the caller's stack frame can go out of scope.
//
extern void ldNotifyDefer(LdSubCache*     cacheP,
                          KjNode*         entityP,
                          LdNotifyOp      op,
                          LdMergeReport*  reportP);



// -----------------------------------------------------------------------------
//
// ldNotifyDispatchPending - flush the per-thread queue.
//
// Runs after the HTTP response has been sent. Invokes
// ldSubscriptionNotifyBatch() which emits one notification per matched
// subscription with data[] carrying every matched pending entity in
// encounter-order. Queue is cleared after dispatch.
//
extern void ldNotifyDispatchPending(void);

#endif  // SWNGSILD_LDNOTIFYDEFER_H_
