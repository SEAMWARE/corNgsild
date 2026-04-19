#ifndef SWNGSILD_LDENTITYATTRSSET_H_
#define SWNGSILD_LDENTITYATTRSSET_H_

//
// FILE            ldEntityAttrsSet.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Generic "set attrs/dsKeys on a target entity" — the primitive behind
// POST /entities/{id}/attrs (Append Attributes, § 5.6.3) and reusable by
// any endpoint that wants to graft a fragment into an existing entity
// without RFC 7396 recursion.
//
// Contract: for each top-level attribute in fragment (storage form),
// and for each dsKey-keyed instance inside:
//   - target has matching (attrName, dsKey) → replace instance,
//     preserving createdAt, bumping modifiedAt.
//   - otherwise → append instance. createdAt stamped to ts, modifiedAt
//     stamped to ts.
// Keywords at top level:
//   - type → union into target's type array.
//   - scope → replaced when overwriteScope=true, union-appended otherwise.
// Entity modifiedAt is bumped if anything changed.
//
// Caller is responsible for per-op noOverwrite classification (if an
// attr/instance should be skipped, remove it from fragment before calling).
//

#include <stdbool.h>                                  // bool
#include <stdint.h>                                   // uint64_t

#include "kjson/KjNode.h"                             // KjNode
#include "kjson/kjson.h"                              // Kjson

#include "swNgsild/ldEntityMerge.h"                   // LdMergeReport



// -----------------------------------------------------------------------------
//
// ldEntityAttrsSet - apply a fragment to a target entity (set-or-append).
//
// target          mutated in place.
// fragment        NOT mutated — clones are inserted into target.
// overwriteScope  true → fragment's scope replaces target's; false → union.
// ts              nanoseconds to stamp as modifiedAt on touched objects.
// reportP         optional — when non-NULL, per-attr change records are
//                 appended to reportP->changes (request-scoped allocator).
// targetAllocP    kjson allocator for nodes grafted into target (NULL for
//                 malloc-backed stores).
//
extern void ldEntityAttrsSet(KjNode*         target,
                             KjNode*         fragment,
                             bool            overwriteScope,
                             uint64_t        ts,
                             LdMergeReport*  reportP,
                             Kjson*          targetAllocP);

#endif  // SWNGSILD_LDENTITYATTRSSET_H_
