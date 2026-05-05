#ifndef SWNGSILD_LDDISTOP_H_
#define SWNGSILD_LDDISTOP_H_

//
// FILE            ldDistOp.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Shared distributed-operation plumbing used by every NGSI-LD entity
// write / read service routine. Each endpoint composes its own URL
// and (if applicable) body, then calls ldDistOpSend(); this module
// handles header composition (Via + tenant + contextSourceInfo),
// plugin lookup, timeout wiring, counter updates, and a common
// BatchEntityError append helper.
//
#include <stdbool.h>                                   // bool

#include "kjson/KjNode.h"                              // KjNode
#include "swRest/SwRestVerb.h"                         // SwRestVerb

#include "swNgsild/LdRegCache.h"                       // LdRegCacheItem



// -----------------------------------------------------------------------------
//
// ldDistOpLoopDetected - true if the incoming request's Via header carries
// our own per-tenant alias. Callers should skip the dispatch pass (but NOT
// fail the request) — local processing and non-looping CSRs still run.
//
// Accepts NULL ownAlias (broker started without alias) → returns false.
//
extern bool ldDistOpLoopDetected(const char* ownAlias);



// -----------------------------------------------------------------------------
//
// ldDistOpCsrWouldLoop - true if forwarding to this CSR would create a loop
//
// § 5.12 match rule: "no registration shall match if the CSourceRegistration
// contextSourceAlias can be found within the listing of previously
// encountered Context Sources". Dispatcher per-CSR pre-check:
//   - csr->csourceAlias unknown (NULL) → false (can't decide, proceed).
//   - csr->csourceAlias in incoming Via chain → true (skip this CSR).
//   - csr->csourceAlias equals our own alias → true (pointing at self).
//
// This is the PROACTIVE form — distinct from ldDistOpLoopDetected which
// reacts to our own alias already appearing in the chain at entry.
//
extern bool ldDistOpCsrWouldLoop(LdRegCacheItem* csr, const char* ownAlias);



// -----------------------------------------------------------------------------
//
// ldDistOpSend - send one CSR forward and update its counters
//
// Common machinery shared by every dispatcher:
//   - Resolves the forwarding plugin by endpoint scheme.
//   - Builds the outbound header set:
//       * Content-Type = contentType arg (or "application/ld+json" default
//         for POST/PATCH/PUT with a body; skipped for GET/DELETE).
//       * Inherited Via headers from the incoming request.
//       * Own-alias Via entry.
//       * NGSILD-Tenant from csr->tenant (§ 5.2.9 rewrite).
//       * contextSourceInfo entries (§ 5.2.22 / 5.2.23), with
//         well-known keys (accept / contentType) mapped to real HTTP
//         headers and banned keys (Content-Length, Host, NGSILD-Tenant,
//         jsonldContext, ngsildConformance) silently dropped.
//   - Passes `csr->timeoutMs` as the per-CSR request timeout.
//   - Increments csr->timesSent. On transport or HTTP error: bumps
//     timesFailed + lastFailure. On 2xx: updates lastSuccess.
//
// verb       HTTP verb (SwVerbGet / SwVerbPost / SwVerbDelete / ...).
// url        Full URL including any query string. Caller composes it.
// body       Request body (NULL for GET/DELETE). Lifetime: request kalloc.
// bodyLen    Body length; ignored when body == NULL.
// ownAlias   This broker's per-tenant alias for Via (NULL = skip Via).
// errorDetailPP  Out-pointer for a transport error description. Set to
//                a message string on transport failure. Lifetime: request
//                kalloc. NULL is legal on success.
//
// Returns the upstream HTTP status (2xx on success). On transport error
// returns 502 and populates *errorDetailPP.
//
extern int ldDistOpSend(LdRegCacheItem*  csr,
                        SwRestVerb       verb,
                        const char*      url,
                        const char*      body,
                        int              bodyLen,
                        const char*      ownAlias,
                        const char**     errorDetailPP);



// -----------------------------------------------------------------------------
//
// ldDistOpSendReceive - like ldDistOpSend, but also returns the response
// body (used by GET forwards which must parse the upstream payload).
//
// responseBodyPP / responseBodyLenP are out-parameters. On success with a
// non-empty body they point into the request kalloc; empty-body cases
// leave them NULL/0. Both may be NULL — behaves like ldDistOpSend then.
//
extern int ldDistOpSendReceive(LdRegCacheItem*  csr,
                               SwRestVerb       verb,
                               const char*      url,
                               const char*      body,
                               int              bodyLen,
                               const char*      ownAlias,
                               const char**     errorDetailPP,
                               char**           responseBodyPP,
                               int*             responseBodyLenP);

// ldDistOpSendReceiveEx - same as ldDistOpSendReceive but also injects
// an additional set of request headers (e.g. NGSILD-EntityMap on
// distributed entity-map pagination per § 6.4.3.2 / § 5.14.4.4).
// Pass extraHeaderV=NULL, extraHeaderCount=0 for no extras.
extern int ldDistOpSendReceiveEx(LdRegCacheItem*  csr,
                                 SwRestVerb       verb,
                                 const char*      url,
                                 const char*      body,
                                 int              bodyLen,
                                 const char*      ownAlias,
                                 SwRestKeyValue*  extraHeaderV,
                                 int              extraHeaderCount,
                                 const char**     errorDetailPP,
                                 char**           responseBodyPP,
                                 int*             responseBodyLenP);



// -----------------------------------------------------------------------------
//
// ldDistOpBatchErrorAdd - append one BatchEntityError (§ 5.2.17) to errors[]
//
// Used by the decision-matrix at the end of every distributed write op to
// carry per-CSR failures into the 207/409 response body.
//
// errorDetail may reference short-lived caller storage; a caller-side copy
// is advised when reusing a static buffer.
//
extern void ldDistOpBatchErrorAdd(KjNode*      errorsArrayP,
                                  const char*  entityId,
                                  const char*  errorType,
                                  const char*  errorTitle,
                                  const char*  errorDetail,
                                  const char*  regId);



// -----------------------------------------------------------------------------
//
// ldBatchErrorsSingleStatus - if every entry in errors[] has the same error
// type, return the corresponding HTTP status; otherwise -1 (caller -> 207).
//
extern int ldBatchErrorsSingleStatus(KjNode* errorsArrayP);



// -----------------------------------------------------------------------------
//
// ldBatchErrorAsProblemDetails - clone the first error[].error into a
// standalone ProblemDetails tree (type/title/detail), suitable as the body
// of a single-status response.
//
extern KjNode* ldBatchErrorAsProblemDetails(KjNode* errorsArrayP);



// -----------------------------------------------------------------------------
//
// ldDistOpForwardFailureReason - canonical "reason" string for notCreated
//
// Returns a thread-local buffer (safe for immediate use; overwritten on
// the next call in the same thread).
//
extern const char* ldDistOpForwardFailureReason(int upCode, const char* upErr);

#endif  // SWNGSILD_LDDISTOP_H_
