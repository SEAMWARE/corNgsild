//
// FILE            ldDistMerge.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// § 4.5.5.3 — Processing of Conflicting Attributes during multi-source
// merge. See header for the algorithm.
//
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // int64_t
#include <string.h>                                      // strcmp

#include "kjson/KjNode.h"                                // KjNode
#include "kjson/kjLookup.h"                              // kjLookup
#include "kjson/kjBuilder.h"                             // kjChildRemove, kjArray, kjString, kjChildAdd
#include "kjson/kjClone.h"                               // kjClone
#include "kjson/kjChildReplace.h"                        // kjChildReplace

#include "corNgsild/LdVocab.h"                            // LD_VOCAB_OBSERVED_AT, LD_VOCAB_MODIFIED_AT, LD_VOCAB_EXPIRES_AT, LD_VOCAB_SCOPE
#include "corNgsild/ldCheckDateTime.h"                    // ldIsoToNanoseconds
#include "corNgsild/ldDistMerge.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// nodeNs - extract a nanosecond timestamp from a KjNode
//
// Storage format usually has system timestamps as KjInteger (epoch-ns)
// already, but accept ISO-8601 KjString too for robustness against
// payloads that haven't passed through normalization yet.
// Returns 0 when nodeP is NULL or unparsable.
//
static int64_t nodeNs(KjNode* nodeP)
{
  if (nodeP == NULL)
    return 0;

  if (nodeP->type == KjInt)
    return (int64_t) nodeP->value.i;

  if (nodeP->type == KjString)
    return ldIsoToNanoseconds(nodeP->value.s);

  return 0;
}



// -----------------------------------------------------------------------------
//
// ldDistInstanceIsExpired -
//
bool ldDistInstanceIsExpired(KjNode* instP, int64_t nowNs)
{
  if (instP == NULL)
    return false;

  int64_t exp = nodeNs(kjLookup(instP, LD_VOCAB_EXPIRES_AT));
  if (exp == 0)
    return false;  // no expiresAt → never expires

  return exp <= nowNs;
}



// -----------------------------------------------------------------------------
//
// ldDistInstanceShouldReplace -
//
bool ldDistInstanceShouldReplace(KjNode* destInstP, KjNode* srcInstP, int64_t nowNs)
{
  if (srcInstP == NULL)  return false;
  if (destInstP == NULL) return true;

  // Step 1 — expiration. A non-expired instance always beats an expired
  // one. Two expired or two non-expired instances proceed to the
  // timestamp comparison.
  bool destExpired = ldDistInstanceIsExpired(destInstP, nowNs);
  bool srcExpired  = ldDistInstanceIsExpired(srcInstP,  nowNs);
  if (srcExpired && !destExpired) return false;
  if (!srcExpired && destExpired) return true;

  // Step 2 — observedAt. If at least one candidate has it, only those
  // candidates compete; instances without observedAt are eliminated when
  // any other has it (per spec "if present" reading).
  int64_t destObs = nodeNs(kjLookup(destInstP, LD_VOCAB_OBSERVED_AT));
  int64_t srcObs  = nodeNs(kjLookup(srcInstP,  LD_VOCAB_OBSERVED_AT));

  if (destObs > 0 || srcObs > 0)
  {
    if (srcObs == 0)  return false;   // dest has it, src doesn't
    if (destObs == 0) return true;    // src has it, dest doesn't
    return srcObs > destObs;
  }

  // Step 3 — fall back to modifiedAt.
  int64_t destMod = nodeNs(kjLookup(destInstP, LD_VOCAB_MODIFIED_AT));
  int64_t srcMod  = nodeNs(kjLookup(srcInstP,  LD_VOCAB_MODIFIED_AT));

  if (srcMod > destMod) return true;

  // Step 4 — indeterminate. Spec says "shall choose at random"; we keep
  // the existing dest for determinism.
  return false;
}



// -----------------------------------------------------------------------------
//
// ldDistExpiresAtReconcile -
//
void ldDistExpiresAtReconcile(KjNode* destP, KjNode* srcP)
{
  if (destP == NULL || srcP == NULL)
    return;

  KjNode* destExpP = kjLookup(destP, LD_VOCAB_EXPIRES_AT);
  if (destExpP == NULL)                 // an earlier version wasn't transient — stays that way
    return;

  KjNode* srcExpP = kjLookup(srcP, LD_VOCAB_EXPIRES_AT);
  if (srcExpP == NULL)
  {
    // This version isn't transient → the merged Entity isn't either.
    kjChildRemove(destP, destExpP);
    return;
  }

  // Both versions are transient — the latest DateTime wins.
  if (nodeNs(srcExpP) > nodeNs(destExpP))
  {
    destExpP->type  = srcExpP->type;
    destExpP->value = srcExpP->value;
  }
}



// -----------------------------------------------------------------------------
//
// scopeValuesInto - add the Scopes of a "scope" member (String or Array) to an array, without duplicates
//
static void scopeValuesInto(KjNode* arrayP, KjNode* scopeP, Kjson* allocP)
{
  bool    isArray = (scopeP->type == KjArray);
  KjNode* valueP  = (isArray == true) ? scopeP->value.firstChildP : scopeP;

  while (valueP != NULL)
  {
    if (valueP->type == KjString)
    {
      bool present = false;

      for (KjNode* haveP = arrayP->value.firstChildP; haveP != NULL; haveP = haveP->next)
      {
        if (strcmp(haveP->value.s, valueP->value.s) == 0)
        {
          present = true;
          break;
        }
      }

      if (present == false)
        kjChildAdd(arrayP, kjString(allocP, NULL, valueP->value.s));
    }

    // A String "scope" holds the single value that is scopeP itself - its 'next' belongs to the Entity
    valueP = (isArray == true) ? valueP->next : NULL;
  }
}



// -----------------------------------------------------------------------------
//
// ldDistScopeMerge -
//
void ldDistScopeMerge(KjNode* destP, KjNode* srcP, Kjson* allocP)
{
  if (destP == NULL || srcP == NULL)
    return;

  KjNode* srcScopeP = kjLookup(srcP, LD_VOCAB_SCOPE);
  if (srcScopeP == NULL)
    return;

  KjNode* destScopeP = kjLookup(destP, LD_VOCAB_SCOPE);

  if (destScopeP == NULL)
  {
    KjNode* cloneP = kjClone(allocP, srcScopeP);

    cloneP->name = (char*) LD_VOCAB_SCOPE;
    kjChildAdd(destP, cloneP);
    return;
  }

  KjNode* unionP = kjArray(allocP, LD_VOCAB_SCOPE);

  scopeValuesInto(unionP, destScopeP, allocP);
  scopeValuesInto(unionP, srcScopeP,  allocP);

  kjChildRemove(destP, destScopeP);

  //
  // § 5.2.7: the value of scope "is represented as a JSON array in case there is more than one
  // Scope" - so a union that came out as a single Scope goes back to a bare String.
  //
  KjNode* onlyP = unionP->value.firstChildP;

  if ((onlyP != NULL) && (onlyP->next == NULL))
    kjChildAdd(destP, kjString(allocP, LD_VOCAB_SCOPE, onlyP->value.s));
  else
    kjChildAdd(destP, unionP);
}




// -----------------------------------------------------------------------------
//
// ldDistMergeSourceInto -
//
void ldDistMergeSourceInto(KjNode* destP, KjNode* srcP, int64_t nowNs, Kjson* kjsonP, bool clone)
{
  if ((destP == NULL) || (srcP == NULL) || (destP->type != KjObject) || (srcP->type != KjObject))
    return;

  //
  // The non-reified entity-level expiresAt is not an Attribute and takes its own
  // route (unanimous across versions or gone), and § 5.2.7 unions the Scopes.
  // Both belong to "merge one version in" - a caller that did the instance loop
  // and forgot these two produced a subtly wrong Entity, which is exactly why
  // this function exists.
  //
  ldDistExpiresAtReconcile(destP, srcP);
  ldDistScopeMerge(destP, srcP, kjsonP);

  KjNode* srcAttrP = srcP->value.firstChildP;

  while (srcAttrP != NULL)
  {
    KjNode* nextSrcAttr = srcAttrP->next;

    //
    // _id is a storage artefact of the mongoc DB model, not an Attribute. It is
    // skipped here so a snapshot merge (which works on trees straight out of the
    // driver) needs no filtering of its own.
    //
    if ((srcAttrP->name == NULL) || (srcAttrP->name[0] == '@') ||
        (strcmp(srcAttrP->name, "id")   == 0) ||
        (strcmp(srcAttrP->name, "_id")  == 0) ||
        (strcmp(srcAttrP->name, "type") == 0) ||
        (srcAttrP->type != KjObject))
    {
      srcAttrP = nextSrcAttr;
      continue;
    }

    KjNode* destAttrP = kjLookup(destP, srcAttrP->name);
    bool    fresh     = (destAttrP == NULL);

    if (fresh)
    {
      destAttrP = kjObject(kjsonP, srcAttrP->name);
      kjChildAdd(destP, destAttrP);
    }

    KjNode* srcInstP = srcAttrP->value.firstChildP;

    while (srcInstP != NULL)
    {
      KjNode* nextSrcInst = srcInstP->next;
      KjNode* destInstP   = kjLookup(destAttrP, srcInstP->name);

      //
      // Rule 1, on the side already assembled. Done BEFORE the src-side check,
      // so that two expired candidates leave nothing behind rather than leaving
      // whichever of them was looked at first.
      //
      if ((destInstP != NULL) && ldDistInstanceIsExpired(destInstP, nowNs))
      {
        kjChildRemove(destAttrP, destInstP);
        destInstP = NULL;
      }

      // Rule 1, on the arriving side. An expired candidate never wins, not even
      // uncontested.
      if (ldDistInstanceIsExpired(srcInstP, nowNs))
      {
        srcInstP = nextSrcInst;
        continue;
      }

      if ((destInstP == NULL) || ldDistInstanceShouldReplace(destInstP, srcInstP, nowNs))
      {
        KjNode* winnerP;

        if (clone)
          winnerP = kjClone(kjsonP, srcInstP);
        else
        {
          // Unlink before adding: kjChildAdd re-links the node, which would
          // truncate srcAttrP's list from srcInstP onwards.
          kjChildRemove(srcAttrP, srcInstP);
          winnerP = srcInstP;
        }

        if (destInstP == NULL)
          kjChildAdd(destAttrP, winnerP);
        else
          kjChildReplace(destAttrP, destInstP, winnerP);
      }

      srcInstP = nextSrcInst;
    }

    //
    // An Attribute whose every candidate was expired must not survive as an
    // empty wrapper. Only a wrapper this call created can end up empty - a
    // pre-existing one still holds the dsKeys srcP never mentioned.
    //
    if (fresh && (destAttrP->value.firstChildP == NULL))
      kjChildRemove(destP, destAttrP);

    srcAttrP = nextSrcAttr;
  }
}
