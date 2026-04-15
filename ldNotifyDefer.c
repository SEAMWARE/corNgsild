//
// FILE            ldNotifyDefer.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stddef.h>                               // NULL

#include "swNgsild/ldSubscriptionNotify.h"        // ldSubscriptionNotify
#include "swNgsild/ldNotifyDefer.h"               // Own interface



// -----------------------------------------------------------------------------
//
// Per-thread pending notification
//
// The current thread handles the full request lifecycle (MHD keeps one
// connection on one thread), so thread-local storage is the cheapest way to
// carry the deferred args from the service routine to the post-response hook
// with no locking and no allocation.
//
typedef struct LdNotifyPending
{
  bool            active;
  bool            hasReport;    // true if report was non-NULL at defer time
  LdSubCache*     cacheP;
  KjNode*         entityP;
  LdNotifyOp      op;
  LdMergeReport   report;       // copied by value — caller's stack frame is gone by dispatch time
} LdNotifyPending;

static __thread LdNotifyPending pending;



// -----------------------------------------------------------------------------
//
// ldNotifyDefer -
//
void ldNotifyDefer(LdSubCache* cacheP, KjNode* entityP, LdNotifyOp op, LdMergeReport* reportP)
{
  pending.active    = true;
  pending.cacheP    = cacheP;
  pending.entityP   = entityP;
  pending.op        = op;
  pending.hasReport = (reportP != NULL);
  if (reportP != NULL)
    pending.report = *reportP;  // struct copy — the referenced KjNode* changes tree is arena-allocated and still valid at dispatch
}



// -----------------------------------------------------------------------------
//
// ldNotifyDispatchPending -
//
void ldNotifyDispatchPending(void)
{
  if (!pending.active)
    return;

  LdMergeReport* reportP = pending.hasReport ? &pending.report : NULL;
  ldSubscriptionNotify(pending.cacheP, pending.entityP, pending.op, reportP);

  pending.active    = false;
  pending.hasReport = false;
  pending.cacheP    = NULL;
  pending.entityP   = NULL;
}
