#ifndef COR_NGSILD_LD_REG_SUB_MERGE_H
#define COR_NGSILD_LD_REG_SUB_MERGE_H
//
// FILE            ldRegSubMerge.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include "kalloc/KAlloc.h"                            // KAlloc
#include "corTree/CorNode.h"                           // CorNode



// -----------------------------------------------------------------------------
//
// ldRegSubMerge - apply a Registration/Subscription update fragment to the full
//                 stored document (shallow JSON Merge Patch, RFC 7396 first level)
//
// For each first-level member of `fragment`:
//   - CorNull           → delete the same-named member from `target` (the
//                         delete-marker, already resolved from "urn:ngsi-ld:null"
//                         by the validator),
//   - any other value   → replace/add it in `target` (cloned into `allocP`).
// `id` and `type` are immutable and never merged.
//
// The merge is done broker-side so the DB plugins only ever STORE a complete
// document — the NGSI-LD merge/delete semantics live here, not in each plugin.
//
extern void ldRegSubMerge(CorNode* target, CorNode* fragment, KAlloc* allocP);

#endif  // COR_NGSILD_LD_REG_SUB_MERGE_H
