//
// FILE            ldNotifyDefer.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stddef.h>                              // NULL
#include <stdlib.h>                              // realloc, free

#include "kjson/KjNode.h"                        // KjNode
#include "kjson/kjLookup.h"                      // kjLookup

#include "swNgsild/ldSubscriptionNotify.h"       // ldSubscriptionNotifyBatch, LdNotifyPendingEntry
#include "swNgsild/ldNotifyDefer.h"              // Own interface



// -----------------------------------------------------------------------------
//
// Per-thread deferred-notification queue
//
// MHD keeps one connection on one thread for the full request lifecycle,
// so a thread-local array covers a full batch without locking. The
// accumulator grows exponentially; it's never freed between requests —
// the capacity is retained so subsequent requests on the same thread
// reuse the allocation.
//
#define LD_NOTIFY_PENDING_INITIAL_CAP 16

static __thread LdSubCache*           pendingCache   = NULL;
static __thread LdNotifyPendingEntry* pendingV       = NULL;
static __thread int                   pendingN       = 0;
static __thread int                   pendingCap     = 0;



// -----------------------------------------------------------------------------
//
// ldNotifyDefer -
//
void ldNotifyDefer(LdSubCache* cacheP, KjNode* entityP, LdNotifyOp op, LdMergeReport* reportP)
{
  if (cacheP == NULL || entityP == NULL)
    return;

  // All pending entries within a request share one (sub) cache.
  pendingCache = cacheP;

  if (pendingN >= pendingCap)
  {
    int newCap = (pendingCap == 0) ? LD_NOTIFY_PENDING_INITIAL_CAP : pendingCap * 2;
    LdNotifyPendingEntry* newV = (LdNotifyPendingEntry*) realloc(pendingV, newCap * sizeof(LdNotifyPendingEntry));
    if (newV == NULL)
      return;
    pendingV   = newV;
    pendingCap = newCap;
  }

  pendingV[pendingN].entityP     = entityP;
  pendingV[pendingN].op          = op;
  pendingV[pendingN].hasReport   = (reportP != NULL);
  pendingV[pendingN].deletedAtNs = 0;
  pendingV[pendingN].reasonsMask = 0;
  if (reportP != NULL && reportP->changes != NULL)
  {
    for (KjNode* chP = reportP->changes->value.firstChildP; chP != NULL; chP = chP->next)
    {
      KjNode* reasonP = kjLookup(chP, "reason");
      if (reasonP != NULL && reasonP->type == KjString)
        pendingV[pendingN].reasonsMask |= ldTriggerFromReport(reasonP->value.s);
    }
  }
  if (reportP != NULL)
    pendingV[pendingN].report    = *reportP;  // struct copy; referenced change-list tree lives in the request arena
  pendingN++;
}



// -----------------------------------------------------------------------------
//
// ldNotifyDeferDelete -
//
void ldNotifyDeferDelete(LdSubCache* cacheP, KjNode* entityP, uint64_t deletedAtNs)
{
  if (cacheP == NULL || entityP == NULL)
    return;

  pendingCache = cacheP;

  if (pendingN >= pendingCap)
  {
    int newCap = (pendingCap == 0) ? LD_NOTIFY_PENDING_INITIAL_CAP : pendingCap * 2;
    LdNotifyPendingEntry* newV = (LdNotifyPendingEntry*) realloc(pendingV, newCap * sizeof(LdNotifyPendingEntry));
    if (newV == NULL)
      return;
    pendingV   = newV;
    pendingCap = newCap;
  }

  pendingV[pendingN].entityP     = entityP;
  pendingV[pendingN].op          = LdNotifyEntityDelete;
  pendingV[pendingN].hasReport   = false;
  pendingV[pendingN].deletedAtNs = deletedAtNs;
  pendingV[pendingN].reasonsMask = 0;
  pendingN++;
}



// -----------------------------------------------------------------------------
//
// ldNotifyDispatchPending -
//
void ldNotifyDispatchPending(void)
{
  if (pendingCache == NULL || pendingN == 0)
  {
    pendingN     = 0;
    pendingCache = NULL;
    return;
  }

  ldSubscriptionNotifyBatch(pendingCache, pendingV, pendingN);

  pendingN     = 0;
  pendingCache = NULL;
}



// -----------------------------------------------------------------------------
//
// ldNotifyPendingSaveReset / ldNotifyPendingRestore - swap aside this thread's
// deferred-notification queue around an in-process self-forward (Inc5b).
//
// The inner request drains this __thread queue via the post-response hook; save
// the outer's queue and start the inner empty, then free the inner's (drained)
// buffer and restore the outer's afterward. Removed when Inc6 makes this state
// per-connection.
//
void ldNotifyPendingSaveReset(LdNotifyPendingSaved* s)
{
  s->v = pendingV; s->n = pendingN; s->cap = pendingCap; s->cache = pendingCache;
  pendingV = NULL; pendingN = 0; pendingCap = 0; pendingCache = NULL;
}

void ldNotifyPendingRestore(const LdNotifyPendingSaved* s)
{
  if (pendingV != NULL)
    free(pendingV);
  pendingV = (LdNotifyPendingEntry*) s->v; pendingN = s->n; pendingCap = s->cap;
  pendingCache = (LdSubCache*) s->cache;
}
