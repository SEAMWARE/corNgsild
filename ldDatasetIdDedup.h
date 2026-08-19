#ifndef CORNGSILD_LDDATASETIDDEDUP_H_
#define CORNGSILD_LDDATASETIDDEDUP_H_

//
// FILE            ldDatasetIdDedup.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// § 4.5.5.3 — Processing of Conflicting Attributes for a single LOCAL
// write payload that contains more than one instance with the same
// datasetId for the same Attribute.
//
// Spec algorithm:
//   1. expiresAt in the past → discard.
//   2. Most recent observedAt wins (instances without observedAt
//      eliminated when any other has it).
//   3. Most recent modifiedAt wins.
//   4. Indeterminate → broker's choice.
//
// Local writes don't have modifiedAt yet (stamped at persist time), so
// step 3 collapses. For step 4 we use the project convention "array
// index = time-of-arrival": the last instance in array order wins.
//
// This is distinct from ldDistMerge (multi-source merge), which keeps
// the existing destination on indeterminate (deterministic across
// distop replies).
//
#include <stdint.h>                                      // uint64_t

#include "kjson/KjNode.h"                                // KjNode



// -----------------------------------------------------------------------------
//
// ldDatasetIdDedup - dedupe duplicate datasetId instances in a multi-attr
// array, in-place, applying the § 4.5.5.3 tiebreaker.
//
// arrayP must be a KjArray of attribute-instance objects (API shape).
// nowNs is used for the expiresAt check.
//
extern void ldDatasetIdDedup(KjNode* arrayP, int64_t nowNs);

#endif  // CORNGSILD_LDDATASETIDDEDUP_H_
