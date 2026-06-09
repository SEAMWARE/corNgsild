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
#include "swNgsild/SwNgsild.h"                   // swNgsild (per-conn pending* cache)
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

// Inc6c — the queue lives in the per-connection swNgsild (swNgsild.pendingV/N/Cap +
// swNgsild.pendingCache); its buffer is freed in swNgsildStateFree.



// -----------------------------------------------------------------------------
//
// ldNotifyDefer -
//
void ldNotifyDefer(LdSubCache* cacheP, KjNode* entityP, LdNotifyOp op, LdMergeReport* reportP)
{
  if (cacheP == NULL || entityP == NULL)
    return;

  // All pending entries within a request share one (sub) cache.
  swNgsild.pendingCache = cacheP;

  if (swNgsild.pendingN >= swNgsild.pendingCap)
  {
    int newCap = (swNgsild.pendingCap == 0) ? LD_NOTIFY_PENDING_INITIAL_CAP : swNgsild.pendingCap * 2;
    LdNotifyPendingEntry* newV = (LdNotifyPendingEntry*) realloc(swNgsild.pendingV, newCap * sizeof(LdNotifyPendingEntry));
    if (newV == NULL)
      return;
    swNgsild.pendingV   = newV;
    swNgsild.pendingCap = newCap;
  }

  swNgsild.pendingV[swNgsild.pendingN].entityP     = entityP;
  swNgsild.pendingV[swNgsild.pendingN].op          = op;
  swNgsild.pendingV[swNgsild.pendingN].hasReport   = (reportP != NULL);
  swNgsild.pendingV[swNgsild.pendingN].deletedAtNs = 0;
  swNgsild.pendingV[swNgsild.pendingN].reasonsMask = 0;
  if (reportP != NULL && reportP->changes != NULL)
  {
    for (KjNode* chP = reportP->changes->value.firstChildP; chP != NULL; chP = chP->next)
    {
      KjNode* reasonP = kjLookup(chP, "reason");
      if (reasonP != NULL && reasonP->type == KjString)
        swNgsild.pendingV[swNgsild.pendingN].reasonsMask |= ldTriggerFromReport(reasonP->value.s);
    }
  }
  if (reportP != NULL)
    swNgsild.pendingV[swNgsild.pendingN].report    = *reportP;  // struct copy; referenced change-list tree lives in the request arena
  swNgsild.pendingN++;
}



// -----------------------------------------------------------------------------
//
// ldNotifyDeferDelete -
//
void ldNotifyDeferDelete(LdSubCache* cacheP, KjNode* entityP, uint64_t deletedAtNs)
{
  if (cacheP == NULL || entityP == NULL)
    return;

  swNgsild.pendingCache = cacheP;

  if (swNgsild.pendingN >= swNgsild.pendingCap)
  {
    int newCap = (swNgsild.pendingCap == 0) ? LD_NOTIFY_PENDING_INITIAL_CAP : swNgsild.pendingCap * 2;
    LdNotifyPendingEntry* newV = (LdNotifyPendingEntry*) realloc(swNgsild.pendingV, newCap * sizeof(LdNotifyPendingEntry));
    if (newV == NULL)
      return;
    swNgsild.pendingV   = newV;
    swNgsild.pendingCap = newCap;
  }

  swNgsild.pendingV[swNgsild.pendingN].entityP     = entityP;
  swNgsild.pendingV[swNgsild.pendingN].op          = LdNotifyEntityDelete;
  swNgsild.pendingV[swNgsild.pendingN].hasReport   = false;
  swNgsild.pendingV[swNgsild.pendingN].deletedAtNs = deletedAtNs;
  swNgsild.pendingV[swNgsild.pendingN].reasonsMask = 0;
  swNgsild.pendingN++;
}



// -----------------------------------------------------------------------------
//
// ldNotifyDispatchPending -
//
void ldNotifyDispatchPending(void)
{
  if (swNgsild.pendingCache == NULL || swNgsild.pendingN == 0)
  {
    swNgsild.pendingN     = 0;
    swNgsild.pendingCache = NULL;
    return;
  }

  ldSubscriptionNotifyBatch(swNgsild.pendingCache, swNgsild.pendingV, swNgsild.pendingN);

  swNgsild.pendingN     = 0;
  swNgsild.pendingCache = NULL;
}

