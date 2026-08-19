//
// FILE            ldDatasetIdDedup.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// § 4.5.5.3 dedup for a single local-write payload — see header.
//
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // int64_t
#include <string.h>                                      // strcmp

#include "kjson/KjNode.h"                                // KjNode
#include "kjson/kjLookup.h"                              // kjLookup
#include "kjson/kjBuilder.h"                             // kjChildRemove

#include "corNgsild/LdVocab.h"                            // LD_VOCAB_DATASET_ID, LD_VOCAB_OBSERVED_AT, LD_VOCAB_EXPIRES_AT
#include "corNgsild/ldCheckDateTime.h"                    // ldIsoToNanoseconds
#include "corNgsild/ldDatasetIdDedup.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// nodeNs - extract a nanosecond timestamp from a KjNode (KjInt or KjString)
//
static int64_t nodeNs(KjNode* nodeP)
{
  if (nodeP == NULL)             return 0;
  if (nodeP->type == KjInt)      return (int64_t) nodeP->value.i;
  if (nodeP->type == KjString)   return ldIsoToNanoseconds(nodeP->value.s);
  return 0;
}



// -----------------------------------------------------------------------------
//
// instanceIsExpired - true if the instance has an expiresAt in the past
//
static bool instanceIsExpired(KjNode* instP, int64_t nowNs)
{
  int64_t exp = nodeNs(kjLookup(instP, LD_VOCAB_EXPIRES_AT));
  if (exp == 0) return false;
  return exp <= nowNs;
}



// -----------------------------------------------------------------------------
//
// laterWins - pairwise comparator. Returns true when bP should oust aP per
// § 4.5.5.3. "later in array order" is the indeterminate-tiebreak choice.
//
static bool laterWins(KjNode* aP, KjNode* bP, int64_t nowNs)
{
  bool aExp = instanceIsExpired(aP, nowNs);
  bool bExp = instanceIsExpired(bP, nowNs);
  if (bExp && !aExp) return false;
  if (!bExp && aExp) return true;

  int64_t aObs = nodeNs(kjLookup(aP, LD_VOCAB_OBSERVED_AT));
  int64_t bObs = nodeNs(kjLookup(bP, LD_VOCAB_OBSERVED_AT));

  if (aObs > 0 || bObs > 0)
  {
    if (bObs == 0) return false;
    if (aObs == 0) return true;
    if (bObs != aObs) return bObs > aObs;
    // Equal observedAt → array order
  }

  // Indeterminate by spec — array order: later (bP) wins.
  return true;
}



// -----------------------------------------------------------------------------
//
// sameDataset - true if both instances target the same dataset key. Both
// without datasetId means both target the default instance.
//
static bool sameDataset(KjNode* aP, KjNode* bP)
{
  KjNode* aDs = kjLookup(aP, LD_VOCAB_DATASET_ID);
  KjNode* bDs = kjLookup(bP, LD_VOCAB_DATASET_ID);

  if (aDs == NULL && bDs == NULL) return true;
  if (aDs == NULL || bDs == NULL) return false;
  if (aDs->type != KjString || bDs->type != KjString) return false;
  return strcmp(aDs->value.s, bDs->value.s) == 0;
}



// -----------------------------------------------------------------------------
//
// ldDatasetIdDedup -
//
void ldDatasetIdDedup(KjNode* arrayP, int64_t nowNs)
{
  if (arrayP == NULL || arrayP->type != KjArray) return;

  // Repeated full scans until no duplicate pair remains. The list is
  // typically tiny (single-digit instances) so O(n^2) per dedup is fine.
  bool changed = true;
  while (changed)
  {
    changed = false;
    for (KjNode* aP = arrayP->value.firstChildP; aP != NULL; aP = aP->next)
    {
      if (aP->type != KjObject) continue;

      for (KjNode* bP = aP->next; bP != NULL; bP = bP->next)
      {
        if (bP->type != KjObject) continue;
        if (!sameDataset(aP, bP))   continue;

        KjNode* loserP = laterWins(aP, bP, nowNs) ? aP : bP;
        kjChildRemove(arrayP, loserP);
        changed = true;
        break;  // list mutated — restart outer
      }
      if (changed) break;
    }
  }
}
