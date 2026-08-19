#ifndef CORNGSILD_LD_WRITE_RESULT_H_
#define CORNGSILD_LD_WRITE_RESULT_H_

//
// FILE            ldWriteResult.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                     // bool

#include "kjson/KjNode.h"                                // KjNode



// -----------------------------------------------------------------------------
//
// LdWriteResult - accumulator for a distributed write operation's UpdateResult
//
// A distributed write (append / update / replace / merge / delete of attributes)
// can fan out to several Context Sources plus the local store. Each leg returns
// its own outcome and they must be merged into ONE UpdateResult — the response
// the client sees. This struct is that running aggregate; feed each leg's
// response into ldWriteResultMerge() as it arrives (incrementally).
//
// updated[]     - array of attribute-name strings that were applied somewhere.
// notUpdated[]  - array of NotUpdatedDetails objects (§ 5.2.19):
//                   { "attributeName", "reason", "registrationId" }
//                 Every entry keeps the registrationId of the leg it came from —
//                 the merge never collapses entries across registrations, since
//                 that attribution is the whole point of a 207 BatchOperationResult.
// anyOk         - at least one leg (a CSR or the local store) reported success.
//
typedef struct LdWriteResult
{
  KjNode* updatedP;
  KjNode* notUpdatedP;
  bool    anyOk;
} LdWriteResult;



// -----------------------------------------------------------------------------
//
// ldWriteResultInit - bind the accumulator to caller-owned updated/notUpdated arrays
//
extern void ldWriteResultInit(LdWriteResult* wrP, KjNode* updatedP, KjNode* notUpdatedP);



// -----------------------------------------------------------------------------
//
// ldWriteResultUpdatedAdd - add an attribute name to updated[] (deduplicated)
//
extern void ldWriteResultUpdatedAdd(KjNode* updatedP, const char* attrName);



// -----------------------------------------------------------------------------
//
// ldWriteResultNotUpdatedAdd - push one NotUpdatedDetails entry
//
// Keeps the registrationId and, when statusCode > 0, the upstream HTTP status
// that caused the failure (orion-ld distOpFailure shape). statusCode 0 omits
// the field — for local rejections (e.g. noOverwrite conflict) that have no
// HTTP status of their own.
//
extern void ldWriteResultNotUpdatedAdd(KjNode* notUpdatedP, const char* attrName,
                                       const char* reason, const char* regId, int statusCode);



// -----------------------------------------------------------------------------
//
// ldWriteResultFragUpdated / ldWriteResultFragNotUpdated - record a whole fragment's
// attributes (short names, keywords skipped) into updated[] / notUpdated[]
//
// For legs decided without a CSR round-trip — e.g. a registration that does not
// support the operation, recorded as notUpdated before anything is forwarded.
//
extern void ldWriteResultFragUpdated(KjNode* updatedP, KjNode* fragP);
extern void ldWriteResultFragNotUpdated(KjNode* notUpdatedP, KjNode* fragP,
                                        const char* reason, const char* regId, int statusCode);



// -----------------------------------------------------------------------------
//
// ldWriteResultMerge - merge ONE forwarded Context Source response into the aggregate
//
// Call once per CSR response, as each comes back. The status decides the leg's
// contribution:
//
//   - 2xx and NOT 207  → clean success: every attribute of forwardedFrag → updated[].
//   - 207 Multi-Status → the CSR itself had a partial result. Splice its UpdateResult
//                        nodes in: its updated[] → ours, and its notUpdated[] → ours,
//                        PRESERVING each entry's registrationId (or stamping regId where
//                        the entry carries none). A 207 with no body flags forwardedFrag's
//                        attributes as notUpdated so a 207 can never be lost to a clean 204.
//   - 404 with tolerate404 → benign (idempotent delete / inclusive source that simply
//                        does not hold the entity); contributes nothing.
//   - any other non-2xx → genuine failure: forwardedFrag's attributes → notUpdated[].
//
//   regId           - the registration this request was forwarded to.
//   errorDetail     - low-level reason for a transport/non-2xx failure (may be NULL).
//   responseTree    - the CSR's response body, already parsed at reception (may be NULL).
//                     A 207 splices its updated[]/notUpdated[] nodes straight in.
//   forwardedFrag    - the fragment slice sent to this CSR (its attribute names).
//   tolerate404     - true: treat a 404 from this CSR as benign.
//
extern void ldWriteResultMerge(LdWriteResult* wrP, const char* regId,
                               int statusCode, const char* errorDetail,
                               KjNode* responseTree,
                               KjNode* forwardedFrag, bool tolerate404);

#endif  // CORNGSILD_LD_WRITE_RESULT_H_
