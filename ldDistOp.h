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
#include "swRest/SwRestKeyValue.h"                     // SwRestKeyValue

#include "swNgsild/LdOp.h"                             // LdOp
#include "swNgsild/LdRegCache.h"                       // LdRegCacheItem, LdRegInfo
#include "swJsonld/SwldContext.h"                      // SwldContext



// -----------------------------------------------------------------------------
//
// ldDistOpForwardContext - effective @context for a forward to this CSR
//
// csi.jsonldContext if the CSR declared one; else the incoming request's
// @context when URL-addressable; else core. Emission sites compact the
// forward body with this (swldCompactTreeWith) — buildHeaders uses the same
// pointer internally, so the Link header always matches the body.
//
extern SwldContext* ldDistOpForwardContext(LdRegCacheItem* csr);



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
// ldDistOpEndpointIsSelf - does this endpoint's authority resolve to us?
//
// Compares scheme://host:port against the broker's own ldBrokerHttpEndpoint,
// ignoring path and tenant. Used by the self-forward short-circuit and by the
// § 12.2.2.4 check that a redirect registration names ANOTHER broker.
//
extern bool ldDistOpEndpointIsSelf(const char* endpoint);



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
// § 5.2.34 management.cooldown — endpoint declined until cooldown elapses
extern bool ldDistOpCsrInCooldown(LdRegCacheItem* csr);

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
// -----------------------------------------------------------------------------
//
// LdDistOpBatchItem / LdDistOpBatchResult / ldDistOpSendMulti -
//
// Concurrent fan-out to N CSRs. The whole batch waits at most
// max(per-CSR timeout) instead of the sequential sum. orion-ld uses
// curl_multi_perform for this; swBroker uses the equivalent
// swRestClientMulti engine (non-blocking epoll over N sockets).
//
// Each item carries a caller-built URL and optional body. ldDistOpSendMulti
// adds the standard distop headers (Via / NGSILD-Tenant / contextSourceInfo /
// Link / Content-Type) per CSR itself. resultV must be at least itemCount
// entries long; the caller initialises it to all-zero. Per-CSR counters
// (timesSent / timesFailed / lastSuccess / lastFailure) are updated.
//
// Returns 0 on setup success (regardless of per-CSR outcome); negative on
// setup failure (rare). Per-CSR failures show up as resultV[i].statusCode
// == 0 with errorDetail filled, or statusCode in 4xx/5xx range.
//
typedef struct LdDistOpBatchItem
{
  LdRegCacheItem*  csr;
  const char*      url;
  const char*      body;     // NULL for GET/DELETE
  int              bodyLen;
  bool             hasVerb;  // per-item verb override (mixed-verb batches —
  SwRestVerb       verb;     // e.g. queryEntity GET next to queryBatch POST)
} LdDistOpBatchItem;

typedef struct LdDistOpBatchResult
{
  int          statusCode;        // 0 on transport failure
  char*        responseBody;      // request-kalloc backing buffer; NULL if empty
  int          responseBodyLen;
  KjNode*      responseTree;      // responseBody parsed once at reception; NULL if no/invalid body
  const char*  errorDetail;       // NULL on success; otherwise request-kalloc
  // The @context that travels WITH the response — the URL in the response's
  // json-ld#context Link header (application/json). NULL if the response
  // carried no such Link (then: embedded @context if ld+json, else core).
  // A forwarded read body MUST be expanded via its own context, not the
  // client's request context nor the outgoing forward context.
  const char*  responseContextUrl;
  // true when statusCode 0 was caused by the broker's own per-CSR timeout
  // (SWC_ERR_TIMEOUT), NOT a connection failure. § 6.3.5: a single exclusive/
  // redirect source that fails to respond in time → 504 (vs 502 for any other
  // reason). Left false for cooldown-declined legs (§ 5.2.6.5.3).
  bool         timedOut;
} LdDistOpBatchResult;

extern int ldDistOpSendMulti(LdDistOpBatchItem*     itemV,
                             int                    itemCount,
                             SwRestVerb             verb,
                             const char*            ownAlias,
                             LdDistOpBatchResult*   resultV);



// -----------------------------------------------------------------------------
//
// ldDistOpWarnings - § 6.3.5 comma-joined NGSILD-Warning warn-values
//
// For a completed GET/HEAD forward batch: one `<code> <host[:port]> "<text>"`
// warn-value per source that behaved abnormally, comma-joined. Codes: 199 (no
// response within timeout — statusCode 0), 111 (invalid payload — the 2xx→502
// downgrade), 299 (an HTTP error status received). A 404 (no data), a 2xx/3xx
// success and a cooldown-declined leg are not abnormal and are skipped.
// Request-arena string; NULL if no source qualifies.
//
extern char* ldDistOpWarnings(LdDistOpBatchItem* itemV, LdDistOpBatchResult* resultV, int itemCount);



// -----------------------------------------------------------------------------
//
// ldDistOpBatchErrorAdd - append one BatchEntityError (§ 5.2.17) to errors[]
//
// Used by the decision-matrix at the end of every distributed write op to
// carry per-CSR failures into the 207/409 response body. The error object
// is rendered as a full ProblemDetails (RFC 7807) — type/title/status/detail.
//
// String fields are copied into swRest.kjsonP; callers may pass stack
// buffers without lifetime concerns.
//
extern void ldDistOpBatchErrorAdd(KjNode*      errorsArrayP,
                                  const char*  entityId,
                                  int          statusCode,
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



// -----------------------------------------------------------------------------
//
// LdDistOpEntry / LdDistOpGroup / ldDistOpEntriesBuild + Perform —
// orion-ld-style refactor of the per-route distop boilerplate.
//
// What this owns:
//   - Iterating the 3-or-4 matched groups (excl / redir / incl [/ aux])
//   - Per-CSR pre-checks: endpoint != NULL, ldDistOpCsrWouldLoop,
//     ldRegOpSupported (with mode-specific Conflict emission)
//   - Optional per-RegistrationInfo loop with entityInfoCoversId +
//     infoCoversAttr filters
//   - Concurrent dispatch via swRestClientMulti (entries-Perform delegates
//     to ldDistOpSendMulti)
//
// What stays per-route:
//   - URL composition (assigned to entry->url)
//   - Body composition (entry->body / bodyLen) — NULL for GET/DELETE
//   - Per-route result classification (success counter, per-attr update map,
//     per-fragment error detail formatting, …)
//
// Result: each route's distop block collapses from ~50-60 lines to ~15-20.
//
typedef struct LdDistOpEntry
{
  LdRegCacheItem*   csr;
  LdRegInfo*        riP;          // matched info entry; NULL when not per-RI
  int               modeIdx;      // 0..groupCount-1 (caller maps to detach / opConf semantics)

  // Loop accounting — Build fills these (§ 6.3.18 / § 9.7). wouldLoop is true
  // when forwarding to this CSR would loop (our own alias already in the Via,
  // or the CSR resolves back to us / a transited broker). errorMode mirrors the
  // group's opConflict (exclusive/redirect surface errors; inclusive is silent),
  // so a looped excl/redirect forward becomes 508 Loop Detected while a looped
  // inclusive forward is simply dropped (the local copy serves it).
  bool              wouldLoop;
  bool              errorMode;

  // Caller fills these between Build and Perform:
  const char*       url;
  const char*       body;         // NULL for GET/DELETE
  int               bodyLen;
  void*             tag;          // arbitrary per-route stash (e.g. the source fragment)

  // Result fields — Perform fills these:
  int               statusCode;   // 0 on transport failure
  char*             responseBody;
  int               responseBodyLen;
  const char*       errorDetail;
  // true when statusCode 0 was the broker's own per-CSR timeout (SWC_ERR_TIMEOUT),
  // NOT a connection failure. § 6.3.5: a single exclusive/redirect source that fails
  // to respond in time → 504 Gateway Timeout (vs 502 for any other reason).
  bool              timedOut;
} LdDistOpEntry;

typedef struct LdDistOpGroup
{
  LdRegCacheItem**  matchV;       // matched CSR array (caller-owned, freed by caller)
  int               matchN;
  const char*       modeTag;      // e.g. "exclusive" / "redirect" / "inclusive" / "auxiliary"
  bool              opConflict;   // true → emit Conflict on !ldRegOpSupported; false → silent skip
} LdDistOpGroup;

// Sweep groups, apply pre-checks, allocate one entry per surviving candidate.
// Entries are emitted in group order (the caller's intended composition order).
// For perRi=true, multiple entries may share a csr (one per matching RegistrationInfo).
// For perRi=false, exactly one entry per surviving csr (riP=NULL).
//
// Returns count of entries; *entriesPP points into swRest.kalloc (request arena).
extern int ldDistOpEntriesBuild(
    const LdDistOpGroup  groupV[],
    int                  groupCount,         // 3 (write) or 4 (read with auxiliary)
    const char*          ownAlias,
    LdOp                 op,                 // for ldRegOpSupported
    const char*          opName,             // for Conflict error detail (e.g. "createEntity")
    const char*          entityIdForErrors,  // for Conflict.entityId (may be NULL)
    bool                 perRi,
    const char*          riEntityIdCheck,    // non-NULL → require entityInfoCoversId
    const char*          riAttrIriCheck,     // non-NULL → require infoCoversAttr
    KjNode*              errorsArrayP,
    LdDistOpEntry**      entriesPP);

// Dispatch the built entries concurrently via ldDistOpSendMulti.
extern void ldDistOpEntriesPerform(
    LdDistOpEntry*       entries,
    int                  count,
    SwRestVerb           verb,
    const char*          ownAlias);



// -----------------------------------------------------------------------------
//
// ldDistOpLoopReap - drop loop-blocked entries before Perform (§ 6.3.18)
//
// For entity-level / single-attribute callers whose built entries each map to a
// precise target (the whole entity, or one attribute), this removes every entry
// marked wouldLoop from the array (they must not be forwarded) and sets
// *loopBlockedP = true if any of them was an exclusive/redirect entry (errorMode)
// — meaning the data is held externally and is now unreachable via the loop.
// Inclusive loop-blocks are silent (the local copy serves them) and do NOT set
// the flag. Returns the compacted entry count; the caller Performs the survivors,
// runs its local op, and at its terminal "nothing found / nothing succeeded"
// branch returns 508 instead of 404 when loopBlocked is set.
//
// Multi-attribute callers (merge / replace / append / create) instead inspect
// entry.wouldLoop at their own chop point, where attribute coverage is known.
//
extern int ldDistOpLoopReap(LdDistOpEntry* entries, int count);

#endif  // SWNGSILD_LDDISTOP_H_
