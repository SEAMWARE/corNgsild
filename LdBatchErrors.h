#ifndef CORNGSILD_LD_BATCH_ERRORS_H_
#define CORNGSILD_LD_BATCH_ERRORS_H_

//
// FILE            LdBatchErrors.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// In-memory collector for BatchEntityError entries (§ 5.2.17).
//
// Distributed write operations (postEntities, deleteEntity, purgeEntities,
// replaceEntity, ...) gather per-CSR / per-leg failures into an "errors[]"
// list, then at finalize time the decision matrix picks 204 / 207 / 409
// and — only in the 207 / 409 branches — serialises the list to a
// KjNode tree to ship as the response body.
//
// Holding the list as a plain struct array means the 204 / 201 "no error"
// path pays zero JSON-tree allocation cost.
//

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjson.h"                               // Kjson



typedef struct LdBatchError
{
  const char* entityId;
  int         statusCode;     // HTTP status this entry corresponds to
  const char* errorType;      // ProblemDetails.type
  const char* errorTitle;     // ProblemDetails.title
  const char* errorDetail;    // ProblemDetails.detail
  const char* regId;          // optional — NULL when not associated with a CSR
} LdBatchError;



typedef struct LdBatchErrorList
{
  KAlloc*       allocP;       // arena for entry copies and the eventual tree
  int           count;
  int           cap;
  LdBatchError* entries;
} LdBatchErrorList;



// -----------------------------------------------------------------------------
//
// ldBatchErrorListInit - zero-state initialiser. No allocations.
//
extern void ldBatchErrorListInit(LdBatchErrorList* listP, KAlloc* allocP);



// -----------------------------------------------------------------------------
//
// ldBatchErrorListAdd - append one BatchEntityError. String fields are
// copied into the list's arena (listP->allocP), so callers may pass stack
// buffers or any other transient strings without lifetime concerns.
//
extern void ldBatchErrorListAdd(LdBatchErrorList* listP,
                                const char*       entityId,
                                int               statusCode,
                                const char*       errorType,
                                const char*       errorTitle,
                                const char*       errorDetail,
                                const char*       regId);



// -----------------------------------------------------------------------------
//
// ldBatchErrorListCount - quick accessor used by the decision matrix.
//
static inline int ldBatchErrorListCount(const LdBatchErrorList* listP)
{
  return (listP != NULL) ? listP->count : 0;
}



// -----------------------------------------------------------------------------
//
// ldBatchErrorListToTree - materialise the list into the canonical
// `"errors": [ … ]` KjArray with one BatchEntityError per entry. Called once
// at response time when the decision matrix has settled on 207 / 409. Each
// BatchEntityError carries a ProblemDetails (`error`) with `type`, `title`,
// `status` and `detail` per § 5.2.17.
//
extern KjNode* ldBatchErrorListToTree(const LdBatchErrorList* listP,
                                      Kjson*                  kjsonP);



// -----------------------------------------------------------------------------
//
// ldBatchErrorListSingleStatus - if every entry has the same statusCode,
// return it; otherwise -1. The decision matrix uses this to collapse a
// uniform-error 207 into a single-status 4xx / 5xx where appropriate
// (§ 6.14 / 6.15 / 6.16 / 6.17 / 6.20).
//
extern int ldBatchErrorListSingleStatus(const LdBatchErrorList* listP);



// -----------------------------------------------------------------------------
//
// ldBatchErrorListFirstAsProblemDetails - clone the first entry's
// ProblemDetails into a standalone tree (type/title/status/detail) for use
// as the body of a single-status response.
//
extern KjNode* ldBatchErrorListFirstAsProblemDetails(const LdBatchErrorList* listP,
                                                     Kjson*                  kjsonP);

#endif  // CORNGSILD_LD_BATCH_ERRORS_H_
