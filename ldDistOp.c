//
// FILE            ldDistOp.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//

#include <stdio.h>                                     // snprintf
#include <string.h>                                    // strcmp, strlen, strcpy
#include <strings.h>                                   // strcasecmp
#include <stdint.h>                                    // uint64_t
#include <time.h>                                      // clock_gettime
#include <regex.h>                                     // regexec

#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjBuilder.h"                           // kjObject, kjString, kjChildAdd
#include "kjson/kjLookup.h"                            // kjLookup
#include "kjson/kjClone.h"                             // kjClone

#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "swRest/SwRestState.h"                        // swRest
#include "swRest/SwRestKeyValue.h"                     // SwRestKeyValue
#include "swRest/SwRestVerb.h"                         // SwVerbGet, SwVerbDelete
#include "swRest/swRestClient.h"                       // swRestClientMulti*

#include "swNgsild/LdForwarding.h"                     // LdForwardRequest, LdForwardResponse, LdForwardingPlugin
#include "swNgsild/ldForwarding.h"                     // ldForwardingForEndpoint
#include "swNgsild/LdRegCache.h"                       // LdRegCacheItem
#include "swNgsild/ldRegCache.h"                       // ldRegOpSupported
#include "swNgsild/swNgsild.h"                         // LD_ERROR_CONFLICT
#include "swNgsild/ldCsourceAlias.h"                   // ldViaHasAlias
#include "swNgsild/ldRequestSubstitute.h"              // ldRequestSubstitute
#include "swJsonld/SwldContext.h"                      // SwldContext (forwardCtxP->url for Link header)
#include "swJsonld/swldInit.h"                         // swldCoreContext
#include "swNgsild/ldDistOp.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// ldDistOpLoopDetected -
//
bool ldDistOpLoopDetected(const char* ownAlias)
{
  if (ownAlias == NULL)
    return false;

  return ldViaHasAlias(swRest.in.httpHeaderV, swRest.in.httpHeaderCount, ownAlias);
}



// -----------------------------------------------------------------------------
//
// ldDistOpCsrWouldLoop -
//
bool ldDistOpCsrWouldLoop(LdRegCacheItem* csr, const char* ownAlias)
{
  if (csr == NULL || csr->csourceAlias == NULL)
    return false;

  // Pointing at ourselves
  if (ownAlias != NULL && strcmp(csr->csourceAlias, ownAlias) == 0)
    return true;

  // Pointing at a broker we've already transited
  return ldViaHasAlias(swRest.in.httpHeaderV, swRest.in.httpHeaderCount, csr->csourceAlias);
}



// -----------------------------------------------------------------------------
//
// verbHasBody - true for verbs that carry a request body
//
// Only these inherit the default Content-Type header when building a
// forward. GET / DELETE / HEAD have no body, so Content-Type is omitted
// unless the CSR's contextSourceInfo explicitly sets one.
//
static bool verbHasBody(SwRestVerb verb)
{
  return (verb != SwVerbGet && verb != SwVerbDelete);
}



// -----------------------------------------------------------------------------
//
// buildHeaders - assemble outbound headers for the forward
//
// Shared rules across all write ops:
//   * Content-Type: defaults to "application/json" + Link header for
//     body-carrying verbs (NOT ld+json — array bodies would force one
//     @context copy per element; json+Link carries it once). § 9.5.2
//     csi.jsonldContext also forces json. Only explicit csi.contentType =
//     ld+json overrides; in that case Link is suppressed and the body
//     marshaller is responsible for embedding @context.
//   * Link: emitted whenever the chosen Content-Type is application/json,
//     pointing at the CSR's forwardCtxP URL (csi.jsonldContext if set,
//     else core). Suppressed for ld+json — the @context belongs IN BODY
//     and mixing both is a 400 per [[feedback_context_header_rules]].
//   * Accept: csi.accept when set; otherwise no Accept header (receiver
//     defaults to ld+json per HTTP binding, which is fine on the way
//     back — we'll convert during aggregation if needed).
//   * Via: every incoming Via pass-through, then own-alias as final hop.
//   * NGSILD-Tenant: csr->tenant if set (§ 5.2.9).
//   * Arbitrary csi entries appended verbatim, minus the banned set
//     (§ 4.3.6.5: Content-Length, Host, NGSILD-Tenant, and the TODO
//     keys jsonldContext + ngsildConformance).
//
static SwRestKeyValue* buildHeaders(SwRestVerb     verb,
                                    const char*    ownAlias,
                                    const char*    csrTenant,
                                    char**         csrInfoKV,
                                    SwldContext*   forwardCtxP,
                                    int*           hcP)
{
  bool wantBody = verbHasBody(verb);

  int viaIn = 0;
  for (int i = 0; i < swRest.in.httpHeaderCount; i++)
    if (swRest.in.httpHeaderV[i].key != NULL &&
        strcasecmp(swRest.in.httpHeaderV[i].key, "Via") == 0)
      viaIn++;

  int csiCount = 0;
  if (csrInfoKV != NULL)
    for (int i = 0; csrInfoKV[i] != NULL; i += 2) csiCount++;

  int cap = viaIn + 5 + csiCount;   // Content-Type + Accept + Link + own Via + NGSILD-Tenant + info[]
  SwRestKeyValue* hv = (SwRestKeyValue*) kaAlloc(&swRest.kalloc, cap * sizeof(SwRestKeyValue));
  int hc = 0;

  const char* csiContentType   = NULL;
  const char* csiAccept        = NULL;
  const char* csiJsonldContext = NULL;
  if (csrInfoKV != NULL)
  {
    for (int i = 0; csrInfoKV[i] != NULL; i += 2)
    {
      const char* k = csrInfoKV[i];
      if      (strcasecmp(k, "contentType")   == 0)  csiContentType   = csrInfoKV[i + 1];
      else if (strcasecmp(k, "accept")        == 0)  csiAccept        = csrInfoKV[i + 1];
      else if (strcasecmp(k, "jsonldContext") == 0)  csiJsonldContext = csrInfoKV[i + 1];
    }
  }

  // Pick chosenCT first; the Link decision keys off it.
  //   csiJsonldContext set + body  → application/json (spec § 9.5.2 SHALL,
  //                                   overrides any csi.contentType)
  //   csiContentType set           → take it verbatim
  //   default                      → application/json (avoid ld+json)
  const char* chosenCT;
  if (csiJsonldContext != NULL && wantBody)
    chosenCT = "application/json";
  else if (csiContentType != NULL)
    chosenCT = csiContentType;
  else
    chosenCT = "application/json";

  bool sendingJson = (strcasecmp(chosenCT, "application/json") == 0);

  if (wantBody)
  {
    hv[hc].key   = (char*) "Content-Type";
    hv[hc].value = (char*) chosenCT;
    hc++;
  }

  //
  // Link header: emitted whenever the forward is application/json (in body
  // verbs AND in GET/DELETE where there's no Content-Type but the receiver
  // still needs the context to interpret URL params + response). For
  // application/ld+json the @context goes in the body — Link is forbidden
  // (mix → 400). URL comes from forwardCtxP — csi.jsonldContext if the CSR
  // declared one, else core context.
  //
  if (sendingJson && forwardCtxP != NULL && forwardCtxP->url != NULL)
  {
    const char* url     = forwardCtxP->url;
    const char* suffix  = ">; rel=\"http://www.w3.org/ns/json-ld#context\"; type=\"application/ld+json\"";
    int   urlLen = strlen(url);
    int   sufLen = strlen(suffix);
    char* linkVal = (char*) kaAlloc(&swRest.kalloc, 1 + urlLen + sufLen + 1);
    linkVal[0] = '<';
    strcpy(linkVal + 1, url);
    strcpy(linkVal + 1 + urlLen, suffix);
    hv[hc].key   = (char*) "Link";
    hv[hc].value = linkVal;
    hc++;
  }

  if (csiAccept != NULL)
  {
    hv[hc].key   = (char*) "Accept";
    hv[hc].value = (char*) csiAccept;
    hc++;
  }

  for (int i = 0; i < swRest.in.httpHeaderCount; i++)
  {
    if (swRest.in.httpHeaderV[i].key != NULL &&
        strcasecmp(swRest.in.httpHeaderV[i].key, "Via") == 0)
    {
      hv[hc].key   = (char*) "Via";
      hv[hc].value = swRest.in.httpHeaderV[i].value;
      hc++;
    }
  }

  if (ownAlias != NULL)
  {
    int   aliasLen = strlen(ownAlias);
    char* viaVal   = (char*) kaAlloc(&swRest.kalloc, 4 + aliasLen + 1);
    strcpy(viaVal, "1.1 ");
    strcpy(viaVal + 4, ownAlias);
    hv[hc].key   = (char*) "Via";
    hv[hc].value = viaVal;
    hc++;
  }

  if (csrTenant != NULL && csrTenant[0] != 0)
  {
    hv[hc].key   = (char*) "NGSILD-Tenant";
    hv[hc].value = (char*) csrTenant;
    hc++;
  }

  if (csrInfoKV != NULL)
  {
    for (int i = 0; csrInfoKV[i] != NULL; i += 2)
    {
      const char* k = csrInfoKV[i];

      if (strcasecmp(k, "contentType")       == 0) continue;   // handled
      if (strcasecmp(k, "accept")            == 0) continue;   // handled
      if (strcasecmp(k, "Content-Length")    == 0) continue;   // banned
      if (strcasecmp(k, "Host")              == 0) continue;   // banned
      if (strcasecmp(k, "NGSILD-Tenant")     == 0) continue;   // banned
      if (strcasecmp(k, "jsonldContext")     == 0) continue;   // handled via Link + Content-Type (§ 6.3.19)
      if (strcasecmp(k, "ngsildConformance") == 0) continue;   // TODO

      // § 4.3.6.5 — "urn:ngsi-ld:request" → take from the triggering
      // request's same-named header (or skip if absent).
      const char* v = ldRequestSubstitute(k, csrInfoKV[i + 1]);
      if (v == NULL) continue;

      hv[hc].key   = (char*) k;
      hv[hc].value = (char*) v;
      hc++;
    }
  }

  *hcP = hc;
  return hv;
}



// -----------------------------------------------------------------------------
//
// ldDistOpForwardContext - effective @context for a forward to this CSR
//
// Precedence (§ 9.5.2 + sensible fallback):
//   1. csi.jsonldContext — the CSR explicitly declared the context its
//      source speaks (csr->forwardCtxP != core; forwardCtxP is never NULL).
//   2. The incoming request's @context, when URL-addressable — the source
//      registered no preference, so forward in the vocabulary the client
//      used; the Link header can carry it (url != NULL).
//   3. Core context — nothing better is referencable by URL.
//
// Body compaction and the Link header MUST use the same context — emission
// sites call this and pass the result to swldCompactTreeWith; buildHeaders
// gets the same pointer so the Link URL matches the body's short names.
//
SwldContext* ldDistOpForwardContext(LdRegCacheItem* csr)
{
  if (csr->forwardCtxP != swldCoreContext())
    return csr->forwardCtxP;

  SwldContext* ctxP = swNgsild.contextP;

  // An in-body @context array of ONE element (the common ld+json form
  // ["<url>"]) is equivalent to its element — unwrap so the Link header
  // has a URL to point at. Multi-element/inline contexts must be HOSTED
  // (ImplicitlyCreated served URL, like sub/reg jsonldContext
  // auto-population) — until that lands they fall through to core.
  // FIXME: host multi-element/inline @context for forwards.
  while (ctxP != NULL && ctxP->isArray && ctxP->contexts == 1)
    ctxP = ctxP->contextV[0];

  if (ctxP != NULL && ctxP->url != NULL)
    return ctxP;

  return swldCoreContext();
}



// -----------------------------------------------------------------------------
//
// ldDistOpSendReceive -
//
int ldDistOpSendReceive(LdRegCacheItem*  csr,
                        SwRestVerb       verb,
                        const char*      url,
                        const char*      body,
                        int              bodyLen,
                        const char*      ownAlias,
                        const char**     errorDetailPP,
                        char**           responseBodyPP,
                        int*             responseBodyLenP)
{
  return ldDistOpSendReceiveEx(csr, verb, url, body, bodyLen, ownAlias,
                               NULL, 0, errorDetailPP, responseBodyPP, responseBodyLenP);
}



// -----------------------------------------------------------------------------
//
// ldDistOpSendReceiveEx -
//
// -----------------------------------------------------------------------------
//
// ldDistOpCsrInCooldown - § 5.2.34 management.cooldown
//
// After a forward failure, the endpoint is not contacted again until the
// cooldown has elapsed; the request behaves as an immediate timeout
// ("a timeout error response for the registration is automatically
// returned" / § 6.3.5 of the HTTP binding). A declined request is not an
// attempt: counters and lastFailure stay untouched, so the cooldown ends
// exactly cooldownMs after the real failure.
//
bool ldDistOpCsrInCooldown(LdRegCacheItem* csr)
{
  if (csr->cooldownMs <= 0 || csr->lastFailure == 0)
    return false;

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t nowNs = (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;

  return nowNs < csr->lastFailure + (uint64_t) csr->cooldownMs * 1000000ULL;
}



int ldDistOpSendReceiveEx(LdRegCacheItem*  csr,
                          SwRestVerb       verb,
                          const char*      url,
                          const char*      body,
                          int              bodyLen,
                          const char*      ownAlias,
                          SwRestKeyValue*  extraHeaderV,
                          int              extraHeaderCount,
                          const char**     errorDetailPP,
                          char**           responseBodyPP,
                          int*             responseBodyLenP)
{
  if (errorDetailPP   != NULL) *errorDetailPP   = NULL;
  if (responseBodyPP  != NULL) *responseBodyPP  = NULL;
  if (responseBodyLenP != NULL) *responseBodyLenP = 0;

  const LdForwardingPlugin* plugin = ldForwardingForEndpoint(csr->endpoint);
  if (plugin == NULL)
  {
    if (errorDetailPP != NULL)
      *errorDetailPP = "no forwarding plugin available for endpoint";
    return 502;
  }

  int             hc = 0;
  SwRestKeyValue* hv = buildHeaders(verb, ownAlias, csr->tenant, csr->contextSourceInfoKV, ldDistOpForwardContext(csr), &hc);

  // Append optional extra headers (e.g. NGSILD-EntityMap for entity-map distops)
  if (extraHeaderV != NULL && extraHeaderCount > 0)
  {
    SwRestKeyValue* merged = (SwRestKeyValue*) kaAlloc(&swRest.kalloc, (hc + extraHeaderCount) * sizeof(SwRestKeyValue));
    for (int i = 0; i < hc; i++) merged[i] = hv[i];
    for (int i = 0; i < extraHeaderCount; i++) merged[hc + i] = extraHeaderV[i];
    hv = merged;
    hc += extraHeaderCount;
  }

  LdForwardRequest  req;
  LdForwardResponse resp;

  req.endpoint         = url;
  req.verb             = verb;
  req.headerV          = hv;
  req.headerCount      = hc;
  req.body             = body;
  req.bodyLen          = (body != NULL) ? bodyLen : 0;
  if (ldDistOpCsrInCooldown(csr))
  {
    if (errorDetailPP != NULL)
      *errorDetailPP = (char*) "registration endpoint in cooldown after failure";
    return 504;
  }

  req.connectTimeoutMs = 0;
  req.requestTimeoutMs = csr->timeoutMs;   // § 5.2.34 per-CSR override

  resp.statusCode      = 0;
  resp.headerV         = NULL;
  resp.headerCount     = 0;
  resp.body            = NULL;
  resp.bodyLen         = 0;
  resp.allocP          = &swRest.kalloc;
  resp.error           = 0;
  resp.errorDetail[0]  = 0;

  int rc = plugin->send(&req, &resp);

  // Counters — § 5.2.36 distribution accounting.
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t nowNs = (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
  csr->timesSent++;

  if (rc != 0)
  {
    csr->timesFailed++;
    csr->lastFailure = nowNs;

    if (errorDetailPP != NULL && resp.errorDetail[0] != 0)
    {
      char* d = (char*) kaAlloc(&swRest.kalloc, strlen(resp.errorDetail) + 1);
      strcpy(d, resp.errorDetail);
      *errorDetailPP = d;
    }
    return 502;
  }

  if (resp.statusCode >= 200 && resp.statusCode < 300)
    csr->lastSuccess = nowNs;
  else
  {
    csr->timesFailed++;
    csr->lastFailure = nowNs;
  }

  if (responseBodyPP   != NULL) *responseBodyPP   = resp.body;
  if (responseBodyLenP != NULL) *responseBodyLenP = resp.bodyLen;

  return resp.statusCode;
}



// -----------------------------------------------------------------------------
//
// ldDistOpSend - thin wrapper when the caller doesn't need the response body
//
int ldDistOpSend(LdRegCacheItem*  csr,
                 SwRestVerb       verb,
                 const char*      url,
                 const char*      body,
                 int              bodyLen,
                 const char*      ownAlias,
                 const char**     errorDetailPP)
{
  return ldDistOpSendReceive(csr, verb, url, body, bodyLen, ownAlias,
                             errorDetailPP, NULL, NULL);
}



// -----------------------------------------------------------------------------
//
// ldDistOpSendMulti -
//
// Fan out N CSR forwards concurrently over swRestClientMulti. The per-CSR
// timeout (§ 5.2.34) caps each request individually inside the multi engine;
// the engine itself runs with the max of all CSR timeouts so no single CSR
// stalls peers. Bypasses the LdForwardingPlugin abstraction — HTTP is the
// only transport that exists. A future non-HTTP plugin (swBin, MQTT) would
// need its own batched fan-out.
//
int ldDistOpSendMulti(LdDistOpBatchItem*     itemV,
                      int                    itemCount,
                      SwRestVerb             verb,
                      const char*            ownAlias,
                      LdDistOpBatchResult*   resultV)
{
  if (itemCount <= 0)
    return 0;

  SwRestClientMulti* multi = swRestClientMultiCreate(itemCount);
  if (multi == NULL)
  {
    for (int i = 0; i < itemCount; i++)
      resultV[i].errorDetail = "swRestClientMultiCreate failed";
    return -1;
  }

  // The multi engine takes a single overall budget. Use the highest per-CSR
  // override so a fast peer doesn't pre-empt a slow one; fall back to the
  // process-wide default when no CSR overrides it.
  int batchTimeoutMs = 0;
  for (int i = 0; i < itemCount; i++)
  {
    int t = (itemV[i].csr != NULL) ? itemV[i].csr->timeoutMs : 0;
    if (t > batchTimeoutMs) batchTimeoutMs = t;
  }
  if (batchTimeoutMs <= 0)
    batchTimeoutMs = swRestClientDefaultRequestTimeoutMs;

  // Add every item. Failed adds (capacity exhausted, bad input) get
  // statusCode == 0 + errorDetail filled and skipped at perform time.
  int addedCount = 0;
  int* addIndex = (int*) kaAlloc(&swRest.kalloc, itemCount * sizeof(int));
  for (int i = 0; i < itemCount; i++) addIndex[i] = -1;

  for (int i = 0; i < itemCount; i++)
  {
    LdRegCacheItem* csr = itemV[i].csr;

    if (ldDistOpCsrInCooldown(csr))
    {
      resultV[i].statusCode  = 504;
      resultV[i].errorDetail = "registration endpoint in cooldown after failure";
      continue;
    }

    SwRestVerb itemVerb = itemV[i].hasVerb ? itemV[i].verb : verb;

    int             hc = 0;
    SwRestKeyValue* hv = buildHeaders(itemVerb, ownAlias, csr->tenant, csr->contextSourceInfoKV, ldDistOpForwardContext(csr), &hc);

    int idx = swRestClientMultiAdd(multi,
                                   itemVerb,
                                   itemV[i].url,
                                   hv, hc,
                                   itemV[i].body, itemV[i].bodyLen,
                                   &swRest.kalloc,
                                   NULL);
    if (idx < 0)
    {
      resultV[i].statusCode  = 0;
      resultV[i].errorDetail = "swRestClientMultiAdd failed";
      continue;
    }

    addIndex[i] = idx;
    addedCount++;
  }

  if (addedCount > 0)
    swRestClientMultiPerform(multi, batchTimeoutMs);

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t nowNs = (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;

  for (int i = 0; i < itemCount; i++)
  {
    LdRegCacheItem* csr = itemV[i].csr;
    int             idx = addIndex[i];

    if (idx < 0)
    {
      // add failed — counter still ticks (we tried), errorDetail already set
      csr->timesSent++;
      csr->timesFailed++;
      csr->lastFailure = nowNs;
      continue;
    }

    SwRestClientResponse* resp = swRestClientMultiResponse(multi, idx);
    csr->timesSent++;

    if (resp == NULL || resp->error != 0)
    {
      csr->timesFailed++;
      csr->lastFailure = nowNs;
      resultV[i].statusCode = 0;
      if (resp != NULL && resp->errorDetail[0] != 0)
      {
        char* d = (char*) kaAlloc(&swRest.kalloc, strlen(resp->errorDetail) + 1);
        strcpy(d, resp->errorDetail);
        resultV[i].errorDetail = d;
      }
      else
        resultV[i].errorDetail = "transport failure";
      continue;
    }

    // resp->body points into the multi engine's per-connection read buffer,
    // which swRestClientMultiDestroy frees. Copy into the request kalloc so
    // the caller can keep using it after destroy.
    resultV[i].statusCode      = resp->statusCode;
    resultV[i].responseBodyLen = resp->bodyLen;
    if (resp->body != NULL && resp->bodyLen > 0)
    {
      char* bodyCopy = (char*) kaAlloc(&swRest.kalloc, resp->bodyLen + 1);
      memcpy(bodyCopy, resp->body, resp->bodyLen);
      bodyCopy[resp->bodyLen] = 0;
      resultV[i].responseBody = bodyCopy;
    }
    else
      resultV[i].responseBody = NULL;

    if (resp->statusCode >= 200 && resp->statusCode < 300)
      csr->lastSuccess = nowNs;
    else
    {
      csr->timesFailed++;
      csr->lastFailure = nowNs;
    }
  }

  swRestClientMultiDestroy(multi);
  return 0;
}



// -----------------------------------------------------------------------------
//
// ldDistOpBatchErrorAdd -
//
void ldDistOpBatchErrorAdd(KjNode*      errorsArrayP,
                           const char*  entityId,
                           int          statusCode,
                           const char*  errorType,
                           const char*  errorTitle,
                           const char*  errorDetail,
                           const char*  regId)
{
  if (errorsArrayP == NULL)
    return;

  KjNode* entry = kjObject(swRest.kjsonP, NULL);
  kjChildAdd(entry, kjString(swRest.kjsonP, "entityId", entityId));

  KjNode* pd = kjObject(swRest.kjsonP, "error");
  kjChildAdd(pd, kjString (swRest.kjsonP, "type",   errorType));
  kjChildAdd(pd, kjString (swRest.kjsonP, "title",  errorTitle));
  kjChildAdd(pd, kjInteger(swRest.kjsonP, "status", statusCode));
  kjChildAdd(pd, kjString (swRest.kjsonP, "detail", errorDetail));
  kjChildAdd(entry, pd);

  if (regId != NULL)
    kjChildAdd(entry, kjString(swRest.kjsonP, "registrationId", regId));

  kjChildAdd(errorsArrayP, entry);
}



// -----------------------------------------------------------------------------
//
// ldBatchErrorsSingleStatus -
//
// Spec § 6.14/6.15/6.16/6.17/6.20 reserve 207 Multi-Status for partial /
// mixed-error batches. The all-failed case isn't covered explicitly, so
// 207 is the closest fit — EXCEPT when every entry carries the same
// "global" failure reason that doesn't benefit from per-entity surfacing:
//
//   - ResourceNotFound  → 404  (every entity in the batch was unknown)
//   - Conflict          → 409  (e.g. exclusive CSR refusing the op for all)
//   - AlreadyExists     → 409  (e.g. § 9.3.3 local-write overlap for all)
//
// In those cases a plain ProblemDetails reply is clearer than wrapping
// a uniform error in a 207 envelope. Returns the matching status when the
// errors[] array is uniformly one of those types, -1 otherwise.
//
// Other uniform-error cases (all-400, all-422, …) still produce 207 —
// they tend to carry per-entity detail worth surfacing.
//
int ldBatchErrorsSingleStatus(KjNode* errorsArrayP)
{
  if (errorsArrayP == NULL || errorsArrayP->value.firstChildP == NULL)
    return -1;

  static const struct { const char* type; int status; } collapsable[] = {
    { "https://uri.etsi.org/ngsi-ld/errors/ResourceNotFound", 404 },
    { "https://uri.etsi.org/ngsi-ld/errors/Conflict",         409 },
    { "https://uri.etsi.org/ngsi-ld/errors/AlreadyExists",    409 },
  };
  const int N = (int) (sizeof(collapsable) / sizeof(collapsable[0]));

  int         matchedStatus = -1;
  const char* matchedType   = NULL;

  for (KjNode* entry = errorsArrayP->value.firstChildP; entry != NULL; entry = entry->next)
  {
    KjNode* errP = kjLookup(entry, "error");
    if (errP == NULL || errP->type != KjObject) return -1;

    KjNode* tP = kjLookup(errP, "type");
    if (tP == NULL || tP->type != KjString) return -1;

    if (matchedType == NULL)
    {
      for (int i = 0; i < N; i++)
      {
        if (strcmp(tP->value.s, collapsable[i].type) == 0)
        {
          matchedType   = collapsable[i].type;
          matchedStatus = collapsable[i].status;
          break;
        }
      }
      if (matchedType == NULL) return -1;
    }
    else if (strcmp(tP->value.s, matchedType) != 0)
    {
      return -1;
    }
  }

  return matchedStatus;
}



// -----------------------------------------------------------------------------
//
// ldBatchErrorAsProblemDetails -
//
// Build a plain ProblemDetails tree from a uniform-error errors[] array.
// Used together with ldBatchErrorsSingleStatus when a batch op collapses a
// uniform-error response to a single HTTP status — the body shape switches
// from BatchOperationResult to plain ProblemDetails so it matches what a
// non-batch op would return.
//
// The clone takes type/title from the first error.error block; the detail
// stays generic ("Not Found" alone is clear enough). entityId(s) are
// echoed back as an extension field — `entityId` (string) if a single entry,
// `entityIds` (array) if more — so the client knows WHICH entity(ies)
// caused the failure without parsing the multi-status envelope.
//
KjNode* ldBatchErrorAsProblemDetails(KjNode* errorsArrayP)
{
  if (errorsArrayP == NULL || errorsArrayP->value.firstChildP == NULL)
    return NULL;

  KjNode* first = errorsArrayP->value.firstChildP;
  KjNode* errP  = kjLookup(first, "error");
  if (errP == NULL || errP->type != KjObject)
    return NULL;

  KjNode* pd = kjObject(swRest.kjsonP, NULL);

  // type / title — clone from the first error so the body looks like a
  // standalone ProblemDetails. Drop "detail" (the per-entity detail strings
  // become noise once we collapse — title alone is clear).
  KjNode* typeP  = kjLookup(errP, "type");
  KjNode* titleP = kjLookup(errP, "title");
  if (typeP  != NULL) kjChildAdd(pd, kjClone(swRest.kjsonP, typeP));
  if (titleP != NULL) kjChildAdd(pd, kjClone(swRest.kjsonP, titleP));

  // entityId(s) — extension field (anticipated ETSI ProblemDetails extension).
  int n = 0;
  for (KjNode* e = errorsArrayP->value.firstChildP; e != NULL; e = e->next) n++;

  if (n == 1)
  {
    KjNode* idP = kjLookup(first, "entityId");
    if (idP != NULL && idP->type == KjString)
      kjChildAdd(pd, kjString(swRest.kjsonP, "entityId", idP->value.s));
  }
  else
  {
    KjNode* idsArr = kjArray(swRest.kjsonP, "entityIds");
    for (KjNode* e = errorsArrayP->value.firstChildP; e != NULL; e = e->next)
    {
      KjNode* idP = kjLookup(e, "entityId");
      if (idP != NULL && idP->type == KjString)
        kjChildAdd(idsArr, kjString(swRest.kjsonP, NULL, idP->value.s));
    }
    kjChildAdd(pd, idsArr);
  }

  return pd;
}



// -----------------------------------------------------------------------------
//
// ldDistOpForwardFailureReason -
//
const char* ldDistOpForwardFailureReason(int upCode, const char* upErr)
{
  static __thread char buf[256];

  if (upErr != NULL && upErr[0] != 0)
    snprintf(buf, sizeof(buf), "forward failed: %s", upErr);
  else
    snprintf(buf, sizeof(buf), "forward returned status %d", upCode);

  return buf;
}



// -----------------------------------------------------------------------------
//
// LdDistOpEntry helpers — private inline checks (kept here so the per-route
// duplication of entityInfoCoversId / infoCoversAttr can be removed when
// every distop route is migrated to the new API).
//
static bool ldoEntityInfoCoversId(LdRegInfo* riP, const char* entityId)
{
  if (riP == NULL || entityId == NULL) return true;
  for (LdRegEntityInfo* eiP = riP->entityInfoV; eiP != NULL; eiP = eiP->next)
  {
    if (eiP->id == NULL && eiP->idPatternList == NULL) return true;
    if (eiP->id != NULL && strcmp(eiP->id, entityId) == 0) return true;
    for (LdRegIdPattern* patP = eiP->idPatternList; patP != NULL; patP = patP->next)
      if (regexec(&patP->regex, entityId, 0, NULL, 0) == 0) return true;
  }
  return false;
}

static bool ldoInfoCoversAttr(LdRegInfo* riP, const char* attrIri)
{
  if (riP == NULL || attrIri == NULL) return true;
  if (riP->propertyNamesV == NULL && riP->relationshipNamesV == NULL) return true;
  for (int i = 0; riP->propertyNamesV != NULL && riP->propertyNamesV[i] != NULL; i++)
    if (strcmp(riP->propertyNamesV[i], attrIri) == 0) return true;
  for (int i = 0; riP->relationshipNamesV != NULL && riP->relationshipNamesV[i] != NULL; i++)
    if (strcmp(riP->relationshipNamesV[i], attrIri) == 0) return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// ldDistOpEntriesBuild -
//
int ldDistOpEntriesBuild(const LdDistOpGroup  groupV[],
                         int                  groupCount,
                         const char*          ownAlias,
                         LdOp                 op,
                         const char*          opName,
                         const char*          entityIdForErrors,
                         bool                 perRi,
                         const char*          riEntityIdCheck,
                         const char*          riAttrIriCheck,
                         KjNode*              errorsArrayP,
                         LdDistOpEntry**      entriesPP)
{
  // Upper-bound capacity: sum over all groups of matchN, times max riP count
  // when perRi. Worst case is "every csr has K infoVs all matching" — we don't
  // know K up front, so count riPs per csr.
  int capacity = 0;
  for (int g = 0; g < groupCount; g++)
  {
    for (int i = 0; i < groupV[g].matchN; i++)
    {
      if (!perRi) { capacity++; continue; }
      LdRegCacheItem* csr = groupV[g].matchV[i];
      for (LdRegInfo* riP = csr->infoV; riP != NULL; riP = riP->next) capacity++;
    }
  }

  LdDistOpEntry* entries = NULL;
  if (capacity > 0)
  {
    entries = (LdDistOpEntry*) kaAlloc(&swRest.kalloc, capacity * sizeof(LdDistOpEntry));
    memset(entries, 0, capacity * sizeof(LdDistOpEntry));
  }
  int count = 0;

  for (int g = 0; g < groupCount; g++)
  {
    const LdDistOpGroup* grp = &groupV[g];
    for (int i = 0; i < grp->matchN; i++)
    {
      LdRegCacheItem* csr = grp->matchV[i];

      if (csr->endpoint == NULL) continue;
      if (ldDistOpCsrWouldLoop(csr, ownAlias)) continue;

      if (!ldRegOpSupported(csr, op))
      {
        if (grp->opConflict && errorsArrayP != NULL)
        {
          char detail[256];
          snprintf(detail, sizeof(detail),
                   "%s registration does not support %s",
                   grp->modeTag, opName);
          ldDistOpBatchErrorAdd(errorsArrayP, entityIdForErrors, 409,
                                LD_ERROR_CONFLICT, "Conflict", detail, csr->regId);
        }
        continue;
      }

      if (!perRi)
      {
        entries[count].csr     = csr;
        entries[count].riP     = NULL;
        entries[count].modeIdx = g;
        count++;
        continue;
      }

      for (LdRegInfo* riP = csr->infoV; riP != NULL; riP = riP->next)
      {
        if (riEntityIdCheck != NULL && !ldoEntityInfoCoversId(riP, riEntityIdCheck)) continue;
        if (riAttrIriCheck  != NULL && !ldoInfoCoversAttr(riP, riAttrIriCheck))     continue;

        entries[count].csr     = csr;
        entries[count].riP     = riP;
        entries[count].modeIdx = g;
        count++;
      }
    }
  }

  *entriesPP = entries;
  return count;
}



// -----------------------------------------------------------------------------
//
// ldDistOpEntriesPerform - dispatch built entries concurrently.
//
// Delegates to ldDistOpSendMulti, marshalling between the entry's
// inline result fields and the lower-level Item/Result pair.
//
void ldDistOpEntriesPerform(LdDistOpEntry* entries,
                            int            count,
                            SwRestVerb     verb,
                            const char*    ownAlias)
{
  if (count <= 0) return;

  LdDistOpBatchItem*   items   = (LdDistOpBatchItem*)   kaAlloc(&swRest.kalloc, count * sizeof(LdDistOpBatchItem));
  LdDistOpBatchResult* results = (LdDistOpBatchResult*) kaAlloc(&swRest.kalloc, count * sizeof(LdDistOpBatchResult));
  memset(results, 0, count * sizeof(LdDistOpBatchResult));

  for (int i = 0; i < count; i++)
  {
    items[i].csr     = entries[i].csr;
    items[i].url     = entries[i].url;
    items[i].body    = entries[i].body;
    items[i].bodyLen = entries[i].bodyLen;
  }

  ldDistOpSendMulti(items, count, verb, ownAlias, results);

  for (int i = 0; i < count; i++)
  {
    entries[i].statusCode      = results[i].statusCode;
    entries[i].responseBody    = results[i].responseBody;
    entries[i].responseBodyLen = results[i].responseBodyLen;
    entries[i].errorDetail     = results[i].errorDetail;
  }
}
