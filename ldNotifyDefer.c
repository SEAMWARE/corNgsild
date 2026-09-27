//
// FILE            ldNotifyDefer.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stddef.h>                              // NULL
#include <stdlib.h>                              // realloc, free
#include <string.h>                              // strcmp

#include "corTree/CorNode.h"                     // CorNode
#include "corTree/corTreeLookup.h"               // corTreeLookup

#include "corNgsild/ldSubscriptionNotify.h"       // ldSubscriptionNotifyBatch, LdNotifyPendingEntry
#include "corNgsild/CorNgsild.h"                   // corNgsild (per-conn pending* cache)
#include "corNgsild/ldNotifyDefer.h"              // Own interface



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

// Inc6c — the queue lives in the per-connection corNgsild (corNgsild.pendingV/N/Cap +
// corNgsild.pendingCache); its buffer is freed in corNgsildStateFree.



// -----------------------------------------------------------------------------
//
// nodeDeepEqual - structural equality of two CorNode subtrees
//
// Scalars compare by value; arrays compare element-wise in order; objects
// compare by name-matched members (order-independent). Used to tell whether an
// attribute's stored value actually changed.
//
static bool nodeDeepEqual(CorNode* a, CorNode* b)
{
  if (a == NULL || b == NULL)  return (a == b);
  if (a->type != b->type)      return false;

  switch (a->type)
  {
  case CorString:  return (strcmp(a->value.s, b->value.s) == 0);
  case CorInt:     return (a->value.i == b->value.i);
  case CorFloat:   return (a->value.f == b->value.f);
  case CorBoolean: return (a->value.b == b->value.b);
  case CorNull:    return true;

  case CorArray:
  {
    CorNode* ca = a->value.head;
    CorNode* cb = b->value.head;
    while (ca != NULL && cb != NULL)
    {
      if (!nodeDeepEqual(ca, cb)) return false;
      ca = ca->next;
      cb = cb->next;
    }
    return (ca == NULL && cb == NULL);
  }

  case CorObject:
  {
    int na = 0, nb = 0;
    for (CorNode* c = a->value.head; c != NULL; c = c->next) na++;
    for (CorNode* c = b->value.head; c != NULL; c = c->next) nb++;
    if (na != nb)  return false;
    for (CorNode* c = a->value.head; c != NULL; c = c->next)
    {
      CorNode* m = corTreeLookup(b, c->name);
      if (m == NULL || !nodeDeepEqual(c, m)) return false;
    }
    return true;
  }

  default:
    return false;
  }
}



// -----------------------------------------------------------------------------
//
// attrValueChanged - did the attribute's stored value change?
//
// preWrapper is the deep-cloned old attribute subtree (dataset-keyed wrapper),
// curWrapper the current one in the merged entity. The DB model stores every
// attribute type's value under the "value" key, so a single per-dataset compare
// of "value" covers Property / Relationship / LanguageProperty / … uniformly.
// A dataset instance added or removed also counts as a change.
//
static bool attrValueChanged(CorNode* preWrapper, CorNode* curWrapper)
{
  if (preWrapper == NULL || curWrapper == NULL)
    return true;

  for (CorNode* oldInst = preWrapper->value.head; oldInst != NULL; oldInst = oldInst->next)
  {
    CorNode* newInst = corTreeLookup(curWrapper, oldInst->name);
    if (newInst == NULL)
      return true;  // dataset instance removed
    if (!nodeDeepEqual(corTreeLookup(oldInst, "value"), corTreeLookup(newInst, "value")))
      return true;
  }

  for (CorNode* newInst = curWrapper->value.head; newInst != NULL; newInst = newInst->next)
    if (corTreeLookup(preWrapper, newInst->name) == NULL)
      return true;  // dataset instance added

  return false;
}



// -----------------------------------------------------------------------------
//
// reportHasValueChange - true if any reported change altered an attribute value
//
// A created or deleted attribute is always a value change. A modified attribute
// counts only when its "value" key actually differs (a sub-attribute or
// timestamp-only modification does not).
//
static bool reportHasValueChange(CorNode* entityP, LdMergeReport* reportP)
{
  if (reportP == NULL || reportP->changes == NULL)
    return true;  // no report ⇒ can't prove it's value-neutral ⇒ notify

  for (CorNode* chP = reportP->changes->value.head; chP != NULL; chP = chP->next)
  {
    CorNode*    reasonP = corTreeLookup(chP, "reason");
    const char* reason  = (reasonP != NULL && reasonP->type == CorString) ? reasonP->value.s : "";

    if (strcmp(reason, "attributeModified") != 0)
      return true;  // attributeCreated / attributeDeleted ⇒ value change

    CorNode* attrP   = corTreeLookup(chP, "attr");
    CorNode* curAttrP = (attrP != NULL && attrP->type == CorString) ? corTreeLookup(entityP, attrP->value.s) : NULL;
    if (attrValueChanged(corTreeLookup(chP, "preValue"), curAttrP))
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// ldNotifyDefer -
//
void ldNotifyDefer(LdSubCache* cacheP, CorNode* entityP, LdNotifyOp op, LdMergeReport* reportP)
{
  if (cacheP == NULL || entityP == NULL)
    return;

  // --notifyValueChangeOnly: suppress an update whose attribute value(s) did not
  // change (only sub-attributes / timestamps did). Create / delete always pass a
  // NULL report and so always notify.
  if (ldNotifyValueChangeOnly && (op == LdNotifyEntityUpdate) && (reportP != NULL))
  {
    if (!reportHasValueChange(entityP, reportP))
      return;
  }

  // All pending entries within a request share one (sub) cache.
  corNgsild.pendingCache = cacheP;

  if (corNgsild.pendingN >= corNgsild.pendingCap)
  {
    int newCap = (corNgsild.pendingCap == 0) ? LD_NOTIFY_PENDING_INITIAL_CAP : corNgsild.pendingCap * 2;
    LdNotifyPendingEntry* newV = (LdNotifyPendingEntry*) realloc(corNgsild.pendingV, newCap * sizeof(LdNotifyPendingEntry));
    if (newV == NULL)
      return;
    corNgsild.pendingV   = newV;
    corNgsild.pendingCap = newCap;
  }

  corNgsild.pendingV[corNgsild.pendingN].entityP     = entityP;
  corNgsild.pendingV[corNgsild.pendingN].op          = op;
  corNgsild.pendingV[corNgsild.pendingN].hasReport   = (reportP != NULL);
  corNgsild.pendingV[corNgsild.pendingN].deletedAtNs = 0;
  corNgsild.pendingV[corNgsild.pendingN].reasonsMask = 0;
  if (reportP != NULL && reportP->changes != NULL)
  {
    for (CorNode* chP = reportP->changes->value.head; chP != NULL; chP = chP->next)
    {
      CorNode* reasonP = corTreeLookup(chP, "reason");
      if (reasonP != NULL && reasonP->type == CorString)
        corNgsild.pendingV[corNgsild.pendingN].reasonsMask |= ldTriggerFromReport(reasonP->value.s);
    }
  }
  if (reportP != NULL)
    corNgsild.pendingV[corNgsild.pendingN].report    = *reportP;  // struct copy; referenced change-list tree lives in the request arena
  corNgsild.pendingN++;
}



// -----------------------------------------------------------------------------
//
// ldNotifyDeferDelete -
//
void ldNotifyDeferDelete(LdSubCache* cacheP, CorNode* entityP, uint64_t deletedAtNs)
{
  if (cacheP == NULL || entityP == NULL)
    return;

  corNgsild.pendingCache = cacheP;

  if (corNgsild.pendingN >= corNgsild.pendingCap)
  {
    int newCap = (corNgsild.pendingCap == 0) ? LD_NOTIFY_PENDING_INITIAL_CAP : corNgsild.pendingCap * 2;
    LdNotifyPendingEntry* newV = (LdNotifyPendingEntry*) realloc(corNgsild.pendingV, newCap * sizeof(LdNotifyPendingEntry));
    if (newV == NULL)
      return;
    corNgsild.pendingV   = newV;
    corNgsild.pendingCap = newCap;
  }

  corNgsild.pendingV[corNgsild.pendingN].entityP     = entityP;
  corNgsild.pendingV[corNgsild.pendingN].op          = LdNotifyEntityDelete;
  corNgsild.pendingV[corNgsild.pendingN].hasReport   = false;
  corNgsild.pendingV[corNgsild.pendingN].deletedAtNs = deletedAtNs;
  corNgsild.pendingV[corNgsild.pendingN].reasonsMask = 0;
  corNgsild.pendingN++;
}



// -----------------------------------------------------------------------------
//
// ldNotifyDispatchPending -
//
void ldNotifyDispatchPending(void)
{
  if (corNgsild.pendingCache == NULL || corNgsild.pendingN == 0)
  {
    corNgsild.pendingN     = 0;
    corNgsild.pendingCache = NULL;
    return;
  }

  ldSubscriptionNotifyBatch(corNgsild.pendingCache, corNgsild.pendingV, corNgsild.pendingN);

  corNgsild.pendingN     = 0;
  corNgsild.pendingCache = NULL;
}

