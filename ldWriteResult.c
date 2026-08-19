//
// FILE            ldWriteResult.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <string.h>                                     // strcmp

#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjBuilder.h"                            // kjObject, kjString, kjChildAdd, kjChildRemove
#include "kjson/kjLookup.h"                             // kjLookup

#include "corRest/CorRestState.h"                         // corRest

#include "corJsonld/corLdInit.h"                          // corLdCoreContext

#include "corNgsild/ldIsEntityKeyword.h"                 // ldIsEntityKeyword
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
void ldWriteResultUpdatedAdd(KjNode* updatedP, const char* attrName)
{
  for (KjNode* p = updatedP->value.firstChildP; p != NULL; p = p->next)
    if ((p->type == KjString) && (strcmp(p->value.s, attrName) == 0))
      return;

  kjChildAdd(updatedP, kjString(corRest.kjsonP, NULL, attrName));
}



// -----------------------------------------------------------------------------
//
// ldWriteResultNotUpdatedAdd - push a NotUpdatedDetails entry (§ 5.2.19)
//
void ldWriteResultNotUpdatedAdd(KjNode* notUpdatedP, const char* attrName,
                                const char* reason, const char* regId, int statusCode)
{
  KjNode* entry = kjObject(corRest.kjsonP, NULL);

  kjChildAdd(entry, kjString(corRest.kjsonP, "attributeName", attrName));
  kjChildAdd(entry, kjString(corRest.kjsonP, "reason",         reason));
  if (regId != NULL)
    kjChildAdd(entry, kjString(corRest.kjsonP, "registrationId", regId));
  if (statusCode > 0)
    kjChildAdd(entry, kjInteger(corRest.kjsonP, "statusCode", statusCode));

  kjChildAdd(notUpdatedP, entry);
}



// -----------------------------------------------------------------------------
//
// ldWriteResultInit -
//
void ldWriteResultInit(LdWriteResult* wrP, KjNode* updatedP, KjNode* notUpdatedP)
{
  wrP->updatedP    = updatedP;
  wrP->notUpdatedP = notUpdatedP;
  wrP->anyOk       = false;
}



// -----------------------------------------------------------------------------
//
// ldWriteResultFragUpdated - every non-keyword attr of fragP into updated[]
//
void ldWriteResultFragUpdated(KjNode* updatedP, KjNode* fragP)
{
  if (fragP == NULL)
    return;

  for (KjNode* c = fragP->value.firstChildP; c != NULL; c = c->next)
  {
    if (ldIsEntityKeyword(c->name))
      continue;
    ldWriteResultUpdatedAdd(updatedP, c->name);
  }
}



// -----------------------------------------------------------------------------
//
// ldWriteResultFragNotUpdated - every non-keyword attr of fragP into notUpdated[] (reason + regId)
//
void ldWriteResultFragNotUpdated(KjNode* notUpdatedP, KjNode* fragP, const char* reason, const char* regId, int statusCode)
{
  if (fragP == NULL)
    return;

  for (KjNode* c = fragP->value.firstChildP; c != NULL; c = c->next)
  {
    if (ldIsEntityKeyword(c->name))
      continue;
    ldWriteResultNotUpdatedAdd(notUpdatedP, c->name, reason, regId, statusCode);
  }
}



// -----------------------------------------------------------------------------
//
// updatedHas - is attrName already in updated[]?
//
static bool updatedHas(KjNode* updatedP, const char* attrName)
{
  for (KjNode* p = updatedP->value.firstChildP; p != NULL; p = p->next)
    if ((p->type == KjString) && (strcmp(p->value.s, attrName) == 0))
      return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// mergeRemoteUpdateResult - fold a CSR's 207 UpdateResult tree into the aggregate
//
// The body was already parsed at reception (ldDistOpResultTree); the remote speaks
// UpdateResult { updated[], notUpdated[] } with short attribute names. We SPLICE its
// nodes across (kjChildRemove + kjChildAdd) rather than rebuild them. updated[] names
// fold in (skipping duplicates already aggregated); notUpdated[] entries keep their
// own registrationId when they carry one (the failure happened deeper in the remote's
// own distribution) and otherwise inherit the registration we forwarded to — never
// deduplicated, since per-registration attribution is the point of the 207. Returns
// false when there was no body tree to merge.
//
static bool mergeRemoteUpdateResult(LdWriteResult* wrP, const char* regId, KjNode* bodyP)
{
  if ((bodyP == NULL) || (bodyP->type != KjObject))
    return false;

  KjNode* up = kjLookup(bodyP, "updated");
  if ((up != NULL) && (up->type == KjArray))
  {
    KjNode* a = up->value.firstChildP;
    while (a != NULL)
    {
      KjNode* next = a->next;
      if ((a->type == KjString) && (!updatedHas(wrP->updatedP, a->value.s)))
      {
        kjChildRemove(up, a);
        kjChildAdd(wrP->updatedP, a);
      }
      a = next;
    }
  }

  KjNode* nu = kjLookup(bodyP, "notUpdated");
  if ((nu != NULL) && (nu->type == KjArray))
  {
    KjNode* e = nu->value.firstChildP;
    while (e != NULL)
    {
      KjNode* next = e->next;
      if (e->type == KjObject)
      {
        if (kjLookup(e, "registrationId") == NULL)
          kjChildAdd(e, kjString(corRest.kjsonP, "registrationId", regId));
        kjChildRemove(nu, e);
        kjChildAdd(wrP->notUpdatedP, e);
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
                        KjNode* responseTree,
                        KjNode* forwardedFrag, bool tolerate404)
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
