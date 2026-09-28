//
// FILE            ldWriteResult.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <string.h>                                     // strcmp

#include "corTree/CorNode.h"                            // CorNode
#include "corTree/corTreeBuilder.h"                     // corTreeObject, corTreeString, corTreeChildAdd, corTreeChildRemove
#include "corTree/corTreeLookup.h"                      // corTreeLookup

#include "corRest/CorRestState.h"                         // corRest

#include "corJsonld/corLdInit.h"                          // corLdCoreContext

#include "corNgsild/ldIsEntityKeyword.h"                  // ldIsEntityKeyword
#include "corNgsild/ldDistOp.h"                          // ldDistOpForwardFailureReason

#include "corNgsild/ldWriteResult.h"                     // Own interface



// -----------------------------------------------------------------------------
//
// ldWriteResultUpdatedAdd -
//
// Attribute names go in as the FULLY QUALIFIED name: an UpdateResult only ever
// travels in a 207 partial-success body, and TS 104-176 § 6.2.3 says "Only
// Fully Qualified Names shall be used in the payload body of error or partial
// success responses, as there is no context present". These names used to be
// compacted against the core context, which silently produced short names for
// default-context attributes (and left foreign-context IRIs expanded — so one
// UpdateResult could carry both spellings at once).
//
void ldWriteResultUpdatedAdd(CorNode* updatedP, const char* attrName)
{
  for (CorNode* p = updatedP->value.head; p != NULL; p = p->next)
    if ((p->type == CorString) && (strcmp(p->value.s, attrName) == 0))
      return;

  corTreeChildAdd(updatedP, corTreeString(corRest.kallocP, NULL, attrName));
}



// -----------------------------------------------------------------------------
//
// ldWriteResultNotUpdatedAdd - push a NotUpdatedDetails entry (§ 5.2.19)
//
void ldWriteResultNotUpdatedAdd(CorNode* notUpdatedP, const char* attrName,
                                const char* reason, const char* regId, int statusCode)
{
  CorNode* entry = corTreeObject(corRest.kallocP, NULL);

  corTreeChildAdd(entry, corTreeString(corRest.kallocP, "attributeName", attrName));
  corTreeChildAdd(entry, corTreeString(corRest.kallocP, "reason", reason));
  if (regId != NULL)
    corTreeChildAdd(entry, corTreeString(corRest.kallocP, "registrationId", regId));
  if (statusCode > 0)
    corTreeChildAdd(entry, corTreeInteger(corRest.kallocP, "statusCode", statusCode));

  corTreeChildAdd(notUpdatedP, entry);
}



// -----------------------------------------------------------------------------
//
// ldWriteResultInit -
//
void ldWriteResultInit(LdWriteResult* wrP, CorNode* updatedP, CorNode* notUpdatedP)
{
  wrP->updatedP    = updatedP;
  wrP->notUpdatedP = notUpdatedP;
  wrP->anyOk       = false;
}



// -----------------------------------------------------------------------------
//
// ldWriteResultFragUpdated - every non-keyword attr of fragP into updated[]
//
void ldWriteResultFragUpdated(CorNode* updatedP, CorNode* fragP)
{
  if (fragP == NULL)
    return;

  for (CorNode* c = fragP->value.head; c != NULL; c = c->next)
  {
    if (ldIsEntityMember(c))
      continue;
    ldWriteResultUpdatedAdd(updatedP, c->name);
  }
}



// -----------------------------------------------------------------------------
//
// ldWriteResultFragNotUpdated - every non-keyword attr of fragP into notUpdated[] (reason + regId)
//
void ldWriteResultFragNotUpdated(CorNode* notUpdatedP, CorNode* fragP, const char* reason, const char* regId, int statusCode)
{
  if (fragP == NULL)
    return;

  for (CorNode* c = fragP->value.head; c != NULL; c = c->next)
  {
    if (ldIsEntityMember(c))
      continue;
    ldWriteResultNotUpdatedAdd(notUpdatedP, c->name, reason, regId, statusCode);
  }
}



// -----------------------------------------------------------------------------
//
// updatedHas - is attrName already in updated[]?
//
static bool updatedHas(CorNode* updatedP, const char* attrName)
{
  for (CorNode* p = updatedP->value.head; p != NULL; p = p->next)
    if ((p->type == CorString) && (strcmp(p->value.s, attrName) == 0))
      return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// mergeRemoteUpdateResult - fold a CSR's 207 UpdateResult tree into the aggregate
//
// The body was already parsed at reception (ldDistOpResultTree); the remote speaks
// UpdateResult { updated[], notUpdated[] } with short attribute names. We SPLICE its
// nodes across (corTreeChildRemove + corTreeChildAdd) rather than rebuild them. updated[] names
// fold in (skipping duplicates already aggregated); notUpdated[] entries keep their
// own registrationId when they carry one (the failure happened deeper in the remote's
// own distribution) and otherwise inherit the registration we forwarded to — never
// deduplicated, since per-registration attribution is the point of the 207. Returns
// false when there was no body tree to merge.
//
static bool mergeRemoteUpdateResult(LdWriteResult* wrP, const char* regId, CorNode* bodyP)
{
  if ((bodyP == NULL) || (bodyP->type != CorObject))
    return false;

  CorNode* up = corTreeLookup(bodyP, "updated");
  if ((up != NULL) && (up->type == CorArray))
  {
    CorNode* a = up->value.head;
    while (a != NULL)
    {
      CorNode* next = a->next;
      if ((a->type == CorString) && (!updatedHas(wrP->updatedP, a->value.s)))
      {
        corTreeChildRemove(up, a);
        corTreeChildAdd(wrP->updatedP, a);
      }
      a = next;
    }
  }

  CorNode* nu = corTreeLookup(bodyP, "notUpdated");
  if ((nu != NULL) && (nu->type == CorArray))
  {
    CorNode* e = nu->value.head;
    while (e != NULL)
    {
      CorNode* next = e->next;
      if (e->type == CorObject)
      {
        if (corTreeLookup(e, "registrationId") == NULL)
          corTreeChildAdd(e, corTreeString(corRest.kallocP, "registrationId", regId));
        corTreeChildRemove(nu, e);
        corTreeChildAdd(wrP->notUpdatedP, e);
      }
      e = next;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// ldWriteResultMerge -
//
void ldWriteResultMerge(LdWriteResult* wrP, const char* regId,
                        int statusCode, const char* errorDetail,
                        CorNode* responseTree,
                        CorNode* forwardedFrag, bool tolerate404)
{
  // Clean success — every 2xx that is NOT a 207 Multi-Status.
  if ((statusCode >= 200) && (statusCode < 300) && (statusCode != 207))
  {
    wrP->anyOk = true;
    ldWriteResultFragUpdated(wrP->updatedP, forwardedFrag);
    return;
  }

  // 207 — the CSR itself returned a partial result. Splice its UpdateResult tree
  // in; a 207 must never be lost to a clean 204, so if there is no body flag the
  // whole forwarded slice as not-updated.
  if (statusCode == 207)
  {
    wrP->anyOk = true;
    if (!mergeRemoteUpdateResult(wrP, regId, responseTree))
      ldWriteResultFragNotUpdated(wrP->notUpdatedP, forwardedFrag,
                                  "remote returned 207 Multi-Status without a result body", regId, 207);
    return;
  }

  // 404 — benign for idempotent ops / inclusive sources that simply do not hold
  // the entity (TS 104-175 § 10.2.8.4). Contributes nothing.
  if ((statusCode == 404) && (tolerate404))
    return;

  // Genuine failure — the whole forwarded slice failed at this CSR.
  ldWriteResultFragNotUpdated(wrP->notUpdatedP, forwardedFrag,
                              ldDistOpForwardFailureReason(statusCode, errorDetail), regId, statusCode);
}
