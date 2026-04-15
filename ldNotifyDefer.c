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
  LdSubCache*     cacheP;
  KjNode*         entityP;
  LdNotifyOp      op;
  LdMergeReport*  reportP;
} LdNotifyPending;

static __thread LdNotifyPending pending = { false, NULL, NULL, 0, NULL };



// -----------------------------------------------------------------------------
//
// ldNotifyDefer -
//
void ldNotifyDefer(LdSubCache* cacheP, KjNode* entityP, LdNotifyOp op, LdMergeReport* reportP)
{
  pending.active  = true;
  pending.cacheP  = cacheP;
  pending.entityP = entityP;
  pending.op      = op;
  pending.reportP = reportP;
}



// -----------------------------------------------------------------------------
//
// ldNotifyDispatchPending -
//
void ldNotifyDispatchPending(void)
{
  if (!pending.active)
    return;

  ldSubscriptionNotify(pending.cacheP, pending.entityP, pending.op, pending.reportP);

  pending.active  = false;
  pending.cacheP  = NULL;
  pending.entityP = NULL;
  pending.reportP = NULL;
}
