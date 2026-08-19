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
