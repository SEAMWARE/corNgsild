#ifndef SWNGSILD_LDDISTMERGE_H_
#define SWNGSILD_LDDISTMERGE_H_

//
// FILE            ldDistMerge.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Multi-source attribute-instance conflict resolution per § 4.5.5.3.
//
// When the broker assembles a split-mode response from multiple Context
// Sources, the same (attribute name, datasetId) pair may arrive from
// more than one source. Each instance is a candidate; the merge picks
// one survivor by:
//
//   1. Discard expired instances (expiresAt in the past).
//   2. If any candidate has observedAt, the latest observedAt wins
//      (instances without observedAt don't compete in this case).
//   3. Otherwise, the latest modifiedAt wins.
//   4. Tie / no tiebreaker → keep the existing one (deterministic).
//
// Auxiliary CSRs (§ 4.3.6.2) only fill gaps and don't go through this
// algorithm — they are merged after the main pass on attrs still
// missing from the assembled entity.
//
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // uint64_t

#include "kjson/KjNode.h"                                // KjNode



// -----------------------------------------------------------------------------
//
// ldDistInstanceShouldReplace - pairwise § 4.5.5.3 comparator
//
// Returns true if `srcInstP` should replace `destInstP` as the surviving
// instance for a given (attrName, datasetId). The caller is expected to
// fold this over all candidates with destInstP as the running survivor.
//
// `nowNs` is the broker's request-start time in epoch-nanoseconds, used
// for expiresAt comparison.
//
extern bool ldDistInstanceShouldReplace(KjNode* destInstP, KjNode* srcInstP, int64_t nowNs);



// -----------------------------------------------------------------------------
//
// ldDistInstanceIsExpired - exposed for callers that want to drop an
// instance entirely when it's the only candidate and it's expired.
//
extern bool ldDistInstanceIsExpired(KjNode* instP, int64_t nowNs);

#endif  // SWNGSILD_LDDISTMERGE_H_
