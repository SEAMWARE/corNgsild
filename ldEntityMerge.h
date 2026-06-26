#ifndef SWNGSILD_LDENTITYMERGE_H_
#define SWNGSILD_LDENTITYMERGE_H_

//
// FILE            ldEntityMerge.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//

#include <stdbool.h>                                  // bool
#include <stdint.h>                                   // uint64_t

#include "kjson/KjNode.h"                             // KjNode
#include "kjson/kjson.h"                              // Kjson



// -----------------------------------------------------------------------------
//
// LdMergeReport - per-attribute change record for Merge Entity (PATCH).
//
// changes is a KjArray (allocator: request-scoped) whose children are KjObjects
// with the following members:
//
//   "attr"     : KjString   — expanded IRI of the top-level attribute that changed
//   "reason"   : KjString   — one of "attributeCreated", "attributeModified",
//                             "attributeDeleted"
//   "preValue" : KjObject   — deep-cloned old attribute subtree (dataset-keyed
//                             wrapper), present for "attributeModified" and
//                             "attributeDeleted" only
//
// Granularity is at the level of top-level attributes. Changes to sub-attributes
// (or to specific dataset instances within a multi-instance attribute) surface as
// a single "attributeModified" record on the parent attribute. This matches what
// subscription watchedAttributes / notificationTrigger can match against.
//
// The post-merge state of any attribute is always readable from the target entity
// itself (passed to ldEntityMerge), so the report does not carry postValue.
//
// A subscription matcher uses the report together with:
//   - the caller's patch fragment  (for watchedAttributes matching — an attribute
//                                    being mentioned counts even if the merge
//                                    produced no net change)
//   - the merged target entity     (for projection / q-filter re-eval)
//   - preValue of each record      (to reconstruct pre-state for q-filter diff)
//
typedef struct LdMergeReport
{
  KjNode*  changes;
} LdMergeReport;



// -----------------------------------------------------------------------------
//
// ldEntityMerge - apply Merge Entity (PATCH) to a DB-model target entity.
//
// Both target and fragment must be in DB storage format:
//   - attributes wrapped in dataset-keyed objects ({"@none": {...}, "urn:ds:1": {...}})
//   - attribute instances carry createdAt/modifiedAt
//   - short-form keys expanded to IRIs
// i.e. the form produced by ldApiEntityToDbModel.
//
// target:         mutated in place with the merge result.
// fragment:       NOT mutated — cloned subtrees are inserted into target so the
//                 caller can keep using the fragment afterwards (subscription
//                 matching against watchedAttributes).
// reportP:        out-param. Non-NULL — the function fills changes as described
//                 on LdMergeReport. Caller allocates the struct; the changes
//                 array and the cloned preValue subtrees are allocated from the
//                 request-scoped kjson arena (swRest.kjsonP) since the report is
//                 consumed within the same request.
// ts:             epoch-nanoseconds timestamp to stamp onto modifiedAt for
//                 touched entity/attribute/sub-attribute containers.
// targetAllocP:   kjson allocator for any node newly grafted into target. Must
//                 match target's lifetime — for an in-memory (malloc-backed)
//                 store pass NULL; for a request-scoped buffer pass
//                 swRest.kjsonP.
//
// Returns true on success, false on semantic error (ldError set by callee).
//
// Merge rules (ETSI GS CIM 009 v1.9.1 § 5.5.12 / § 5.6.17, RFC 7396):
//   * A first-level fragment member with value "urn:ngsi-ld:null" deletes the
//     named attribute (if present). Reason: "attributeDeleted".
//   * A fragment attribute that is absent from target is appended. Reason:
//     "attributeCreated".
//   * A fragment attribute that is present in target is merged recursively,
//     with "urn:ngsi-ld:null" at any depth meaning "delete this member".
//     Reason: "attributeModified".
//   * `type` in fragment: the union of fragment types is added to target's
//     type array; new types trigger no attribute-level change record.
//   * `scope` in fragment replaces target's scope.
//   * Any object that gets mutated (attribute, sub-attribute, ..., entity)
//     has its modifiedAt bumped to `ts`.
//
extern bool ldEntityMerge(KjNode*         target,
                          KjNode*         fragment,
                          LdMergeReport*  reportP,
                          uint64_t        ts,
                          struct Kjson*   targetAllocP);



// -----------------------------------------------------------------------------
//
// ldEntityFragmentApply - apply an Entity Fragment with REPLACE/append semantics
// (Update Attributes, Partial Attribute Update, Append Attributes, Replace
// Attribute). Same plumbing as ldEntityMerge but the primary value is replaced
// wholesale instead of being deep-merged. See ldEntityMerge.c.
//
extern bool ldEntityFragmentApply(KjNode*         target,
                                  KjNode*         fragment,
                                  LdMergeReport*  reportP,
                                  uint64_t        ts,
                                  struct Kjson*   targetAllocP);



// -----------------------------------------------------------------------------
//
// ldEntityReplaceReport - change-report by diffing the old vs the new entity
//                         (Replace / PUT): created / modified / deleted attrs.
//
extern void ldEntityReplaceReport(KjNode* oldEntityP, KjNode* newEntityP, LdMergeReport* reportP);

#endif  // SWNGSILD_LDENTITYMERGE_H_
