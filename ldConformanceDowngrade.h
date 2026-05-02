#ifndef SWNGSILD_LDCONFORMANCEDOWNGRADE_H_
#define SWNGSILD_LDCONFORMANCEDOWNGRADE_H_

//
// FILE            ldConformanceDowngrade.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Backwards-compatibility transformation per § 4.3.6.8.
//
// Subscriptions (§ 5.2.12) and CSRs (§ 4.3.6.6) may declare a target
// NGSI-LD spec version they expect via `ngsildConformance` (e.g. "1.5").
// When the target is older than the broker's native version, fields
// introduced after the target must be removed or reformatted before
// the payload leaves the broker.
//
#include <stdbool.h>                                     // bool

#include "kjson/KjNode.h"                                // KjNode
#include "kjson/kjson.h"                                 // Kjson



// -----------------------------------------------------------------------------
//
// ldConformanceParse - parse "M.m" → (majorOut, minorOut). Returns false on
// malformed input. major/minor are 0..32767.
//
extern bool ldConformanceParse(const char* str, short* majorOut, short* minorOut);



// -----------------------------------------------------------------------------
//
// ldConformanceDowngrade - mutate `tree` in place to conform to the
// target version. Both arrays of entities and single-entity trees are
// supported (root may be KjArray or KjObject).
//
// targetMajor/targetMinor 0/0 → no-op.
//
// Per § 4.3.6.8 Tables 4.3.6.8-1/2/3:
//   < 1.9  : strip entity expiresAt, attr expiresAt, attr valueType
//   < 1.8  : reformat ListRelationship → Relationship (first object),
//            JsonProperty/VocabProperty/ListProperty → Property
//   < 1.4  : strip entity scope; reformat LanguageProperty → Property
//   < 1.3  : strip attr datasetId/observedAt/unitCode; collapse
//            multi-instance arrays to one element; collapse multi-type
//            arrays to first element.
//
// kjsonP is needed to allocate replacement nodes.
//
extern void ldConformanceDowngrade(KjNode* treeP, short targetMajor, short targetMinor, Kjson* kjsonP);



#endif  // SWNGSILD_LDCONFORMANCEDOWNGRADE_H_
