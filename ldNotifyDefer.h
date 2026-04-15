//
// FILE            ldNotifyDefer.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef SWNGSILD_LDNOTIFYDEFER_H_
#define SWNGSILD_LDNOTIFYDEFER_H_

#include "kjson/KjNode.h"                      // KjNode
#include "swNgsild/ldSubCache.h"               // LdSubCache
#include "swNgsild/ldSubscriptionNotify.h"     // LdNotifyOp
#include "swNgsild/ldEntityMerge.h"             // LdMergeReport



// -----------------------------------------------------------------------------
//
// ldNotifyDefer - stash subscription-notification work for later dispatch
//
// Replaces an inline call to ldSubscriptionNotify() inside service routines.
// The args are captured in thread-local state and fired by
// ldNotifyDispatchPending() once the HTTP response has been delivered to the
// client (wired via swRestSetPostResponseHook).
//
// IMPORTANT: entityP and reportP must stay valid until ldNotifyDispatchPending
// runs. They do, because both are allocated in the request arena which is
// released *after* the post-response hook.
//
extern void ldNotifyDefer(LdSubCache*     cacheP,
                          KjNode*         entityP,
                          LdNotifyOp      op,
                          LdMergeReport*  reportP);



// -----------------------------------------------------------------------------
//
// ldNotifyDispatchPending - run any notification deferred by this thread
//
// Registered via swRestSetPostResponseHook so swRest invokes it after the
// response has been sent but before the per-request arena is freed.
//
extern void ldNotifyDispatchPending(void);

#endif  // SWNGSILD_LDNOTIFYDEFER_H_
