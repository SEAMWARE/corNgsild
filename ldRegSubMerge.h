#ifndef SW_NGSILD_LD_REG_SUB_MERGE_H
#define SW_NGSILD_LD_REG_SUB_MERGE_H
//
// FILE            ldRegSubMerge.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include "kjson/kjson.h"                              // Kjson
#include "kjson/KjNode.h"                              // KjNode



// -----------------------------------------------------------------------------
//
// ldRegSubMerge - apply a Registration/Subscription update fragment to the full
//                 stored document (shallow JSON Merge Patch, RFC 7396 first level)
//
// For each first-level member of `fragment`:
//   - KjNull            → delete the same-named member from `target` (the
//                         delete-marker, already resolved from "urn:ngsi-ld:null"
//                         by the validator),
//   - any other value   → replace/add it in `target` (cloned into `allocP`).
// `id` and `type` are immutable and never merged.
//
// The merge is done broker-side so the DB plugins only ever STORE a complete
// document — the NGSI-LD merge/delete semantics live here, not in each plugin.
//
extern void ldRegSubMerge(KjNode* target, KjNode* fragment, Kjson* allocP);

#endif  // SW_NGSILD_LD_REG_SUB_MERGE_H
