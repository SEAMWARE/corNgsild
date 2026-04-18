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
// ldDistOpForwardFailureReason - canonical "reason" string for notCreated
//
// Returns a thread-local buffer (safe for immediate use; overwritten on
// the next call in the same thread).
//
extern const char* ldDistOpForwardFailureReason(int upCode, const char* upErr);

#endif  // SWNGSILD_LDDISTOP_H_
