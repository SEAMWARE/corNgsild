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
#include "kjson/kjParse.h"                             // kjParse

#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "ktrace/kTrace.h"                             // KT_T
#include "corRest/CorRestState.h"                        // corRest
#include "corRest/CorRestKeyValue.h"                     // CorRestKeyValue
#include "corRest/CorRestVerb.h"                         // CorVerbGet, CorVerbDelete
#include "corRest/corRestClient.h"                       // corRestClientMulti*
#include "corRest/corRestInit.h"                          // corRestProcessInProcess (self-forward)

#include "corNgsild/LdForwarding.h"                     // LdForwardRequest, LdForwardResponse, LdForwardingPlugin
#include "corNgsild/ldForwarding.h"                     // ldForwardingForEndpoint
#include "corNgsild/LdRegCache.h"                       // LdRegCacheItem
#include "corNgsild/ldRegCache.h"                       // ldRegOpSupported
#include "corNgsild/corNgsild.h"                         // LD_ERROR_CONFLICT
#include "corNgsild/ldCsourceAlias.h"                   // ldViaHasAlias
#include "corNgsild/ldRequestSubstitute.h"              // ldRequestSubstitute
#include "corJsonld/CorLdContext.h"                      // CorLdContext (forwardCtxP->url for Link header)
#include "corJsonld/corLdInit.h"                         // corLdCoreContext
#include "corJsonld/corLdDownload.h"                     // corLdIsCoreContextUrl
#include "corNgsild/ldContextHost.h"                    // ldContextHostVolatile
#include "corNgsild/ldTraceLevels.h"                    // LdTForwardResp
#include "corNgsild/ldDistOp.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// ldForwardTenant - the tenant a forward to this CSR will carry (§ 5.2.9)
//
// The registration's own tenant is a REWRITE - a mapping to a different tenant at the
// context source. Without one, the ORIGINAL request's tenant travels on. Two places need
// exactly this answer and must not drift apart: buildHeaders, which SENDS the tenant, and
// the loop check below, which has to predict the alias the receiver will compute from it.
//
static const char* ldForwardTenant(const char* csrTenant)
{
  if ((csrTenant != NULL) && (csrTenant[0] != 0))
    return csrTenant;

  for (int i = 0; i < corRest.in.httpHeaderCount; i++)
  {
    if ((corRest.in.httpHeaderV[i].key != NULL) &&
        (strcasecmp(corRest.in.httpHeaderV[i].key, "NGSILD-Tenant") == 0))
      return corRest.in.httpHeaderV[i].value;
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// ldCsrAliasForForward - the CSR's alias, scoped to the tenant the forward will carry
//
// § 5.7.5 wants the alias to identify a specific TENANT within a Context Source, and the
// receiver derives its own from the NGSILD-Tenant it is handed - so a comparison is only
// sound when both sides carry the same tenant.
//
// A CSR that rewrites the tenant was PROBED with that tenant (ldRegCacheProbePending passes
// itemP->tenant), so its alias is already tenant-scoped - nothing to add. A CSR WITHOUT a
// rewrite was probed with no tenant at all and answered with the endpoint's DEFAULT-tenant
// alias, while the forward will carry ours. Compose what the receiver will actually compute,
// or a self-loop on a non-default tenant slips past this proactive skip: it is still caught
// by ldDistOpLoopDetected when the request lands back on us, but only after the whole request
// has been dispatched a second time.
//
static const char* ldCsrAliasForForward(LdRegCacheItem* csr)
{
  if ((csr->tenant != NULL) && (csr->tenant[0] != 0))
    return csr->csourceAlias;

  const char* tenant = ldForwardTenant(NULL);
  if ((tenant == NULL) || (tenant[0] == 0))
    return csr->csourceAlias;   // default tenant - the base alias IS the scoped one

  int   aliasLen  = strlen(csr->csourceAlias);
  int   tenantLen = strlen(tenant);
  char* scopedP   = (char*) kaAlloc(&corRest.kalloc, aliasLen + 1 + tenantLen + 1);

  strcpy(scopedP, csr->csourceAlias);
  scopedP[aliasLen] = ':';
  strcpy(scopedP + aliasLen + 1, tenant);

  return scopedP;
}



// -----------------------------------------------------------------------------
//
// ldDistOpLoopDetected -
//
bool ldDistOpLoopDetected(const char* ownAlias)
{
  if (ownAlias == NULL)
    return false;

  return ldViaHasAlias(corRest.in.httpHeaderV, corRest.in.httpHeaderCount, ownAlias);
}



// -----------------------------------------------------------------------------
//
// ldDistOpCsrWouldLoop -
//
bool ldDistOpCsrWouldLoop(LdRegCacheItem* csr, const char* ownAlias)
{
  if (csr == NULL || csr->csourceAlias == NULL)
    return false;

  const char* regId = (csr->regId != NULL) ? csr->regId : "<no id>";
  const char* alias = ldCsrAliasForForward(csr);

  // Pointing at ourselves. This is the single most confusing way for a forward
  // to disappear — an inclusive loop-block is silent by design, so without this
  // line nothing anywhere explains the empty answer. Two brokers that compute
  // the same alias (same executable and port, different hosts) look identical
  // to each other and neither will forward to the other.
  if (ownAlias != NULL && strcmp(alias, ownAlias) == 0)
  {
    KT_T(LdTRegMatch, "%s: matched, but NOT forwarded to: loop — its alias '%s' is our own",
         regId, alias);
    return true;
  }

  // Pointing at a broker we've already transited
  if (ldViaHasAlias(corRest.in.httpHeaderV, corRest.in.httpHeaderCount, alias))
  {
    KT_T(LdTRegMatch, "%s: matched, but NOT forwarded to: loop — '%s' is already in the inbound Via",
         regId, alias);
    return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// verbHasBody - true for verbs that carry a request body
//
// Only these inherit the default Content-Type header when building a
// forward. GET / DELETE / HEAD have no body, so Content-Type is omitted
// unless the CSR's contextSourceInfo explicitly sets one.
//
static bool verbHasBody(CorRestVerb verb)
{
  return (verb != CorVerbGet && verb != CorVerbDelete);
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
static CorRestKeyValue* buildHeaders(CorRestVerb     verb,
                                    const char*    ownAlias,
                                    const char*    csrTenant,
                                    char**         csrInfoKV,
                                    CorLdContext*   forwardCtxP,
                                    int*           hcP)
{
  bool wantBody = verbHasBody(verb);

  int viaIn = 0;
  for (int i = 0; i < corRest.in.httpHeaderCount; i++)
    if (corRest.in.httpHeaderV[i].key != NULL &&
        strcasecmp(corRest.in.httpHeaderV[i].key, "Via") == 0)
      viaIn++;

  int csiCount = 0;
  if (csrInfoKV != NULL)
    for (int i = 0; csrInfoKV[i] != NULL; i += 2) csiCount++;

  int cap = viaIn + 6 + csiCount;   // Content-Type + Accept + Link + NGSILD-Volatile-Context + own Via + NGSILD-Tenant + info[]
  CorRestKeyValue* hv = (CorRestKeyValue*) kaAlloc(&corRest.kalloc, cap * sizeof(CorRestKeyValue));
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
    char* linkVal = (char*) kaAlloc(&corRest.kalloc, 1 + urlLen + sufLen + 1);
    linkVal[0] = '<';
    strcpy(linkVal + 1, url);
    strcpy(linkVal + 1 + urlLen, suffix);
    hv[hc].key   = (char*) "Link";
    hv[hc].value = linkVal;
    hc++;

    // The Link points at a one-shot broker-hosted context (inline / multi
    // element request @context with no URL of its own). Tell the receiver
    // not to cache it — it vanishes after the first fetch. Belt-and-braces
    // with the Cache-Control: no-store the served document also carries.
    if (forwardCtxP->volatileCtx)
    {
      hv[hc].key   = (char*) "NGSILD-Volatile-Context";
      hv[hc].value = (char*) "true";
      hc++;
    }
  }

  // Always send an explicit Accept. Per § 6.2.2 a receiver SHALL assume application/json
  // when no Accept header is present, so sending none is spec-legal — but a non-conformant
  // peer may 406 an Accept-less request. Send it explicitly (the CSR's contextSourceInfo
  // accept if declared, else application/json — what coraine wants back anyway) to protect
  // ourselves against such misbehaving peers. Interop hardening, not a spec requirement.
  hv[hc].key   = (char*) "Accept";
  hv[hc].value = (char*) ((csiAccept != NULL) ? csiAccept : "application/json");
  hc++;

  for (int i = 0; i < corRest.in.httpHeaderCount; i++)
  {
    if (corRest.in.httpHeaderV[i].key != NULL &&
        strcasecmp(corRest.in.httpHeaderV[i].key, "Via") == 0)
    {
      hv[hc].key   = (char*) "Via";
      hv[hc].value = corRest.in.httpHeaderV[i].value;
      hc++;
    }
  }

  if (ownAlias != NULL)
  {
    int   aliasLen = strlen(ownAlias);
    char* viaVal   = (char*) kaAlloc(&corRest.kalloc, 4 + aliasLen + 1);
    strcpy(viaVal, "1.1 ");
    strcpy(viaVal + 4, ownAlias);
    hv[hc].key   = (char*) "Via";
    hv[hc].value = viaVal;
    hc++;
  }

  //
  // NGSILD-Tenant on the forward. The registration's own tenant (csrTenant) is a
  // tenant REWRITE — a mapping to a different tenant at the context source. When
  // the registration carries no such rewrite, the ORIGINAL request's tenant is
  // forwarded so the context source resolves it in the same tenant the consumer
  // addressed. (The inbound NGSILD-Tenant is otherwise banned from pass-through
  // below, so it is picked up explicitly here.)
  //
  const char* fwdTenant = ldForwardTenant(csrTenant);

  if (fwdTenant != NULL && fwdTenant[0] != 0)
  {
    hv[hc].key   = (char*) "NGSILD-Tenant";
    hv[hc].value = (char*) fwdTenant;
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
// forwardHostOrCore - host the in-body @context as a volatile served context,
// or fall back to core when there's nothing to host.
//
// Used when the request's user @context cannot be referenced by URL as-is
// (inline object, or multi-element array): the Link header needs a URL, so
// we mint a one-shot hosted context and forward in that vocabulary.
//
static CorLdContext* forwardHostOrCore(void)
{
  if (corNgsild.userContextBody != NULL)
  {
    CorLdContext* hostedP = ldContextHostVolatile(corNgsild.userContextBody);
    if (hostedP != NULL)
      return hostedP;
  }
  return corLdCoreContext();
}



// -----------------------------------------------------------------------------
//
// ldDistOpForwardContext - effective @context for a forward to this CSR
//
// Precedence (§ 9.5.2 + sensible fallback):
//   1. csi.jsonldContext — the CSR explicitly declared the context its
//      source speaks (csr->forwardCtxP != core; forwardCtxP is never NULL).
//   2. The incoming request's @context — forward in the vocabulary the
//      client used. URL-addressable as-is when possible; an inline /
//      multi-element @context is hosted as a volatile served URL so the
//      Link header can carry it.
//   3. Core context — the client sent only core (nothing to preserve).
//
// Body compaction and the Link header MUST use the same context — emission
// sites call this and pass the result to corLdCompactTreeWith; buildHeaders
// gets the same pointer so the Link URL matches the body's short names.
//
CorLdContext* ldDistOpForwardContext(LdRegCacheItem* csr)
{
  if (csr->forwardCtxP != corLdCoreContext())
    return csr->forwardCtxP;

  CorLdContext* ctxP = corNgsild.contextP;

  // Resolve an in-body @context array to its USER part. Nearly every
  // real request carries ["<user-ctx>", "<core-ctx>"] — often an OLD
  // core version. Core elements are inert for the forward choice: the
  // receiver applies its own core regardless, and core always wins. So
  // skip them; what remains decides:
  //   exactly one URL element → the user context (also covers ["<url>"])
  //   none                    → core (the client really sent only core)
  //   two-plus / inline       → must be HOSTED (volatile served URL) so
  //     the Link header can reference it; a Link header carries a URL, not
  //     an inline object or multi-element array.
  while (ctxP != NULL && ctxP->isArray)
  {
    CorLdContext* soleP = NULL;
    int          users = 0;

    for (int i = 0; i < ctxP->contexts; i++)
    {
      CorLdContext* eP = ctxP->contextV[i];
      if (eP == NULL)                                        continue;
      if (eP->url != NULL && corLdIsCoreContextUrl(eP->url))  continue;
      users++;
      soleP = eP;
    }

    if (users == 0)
      return corLdCoreContext();         // client really sent only core
    if (users == 1)
    {
      ctxP = soleP;                     // unwrap, keep walking
      continue;
    }
    return forwardHostOrCore();         // two-plus user elements → host
  }

  if (ctxP != NULL && ctxP->url != NULL)
    return ctxP;                        // single URL-addressable user ctx

  return forwardHostOrCore();           // inline object (no url) → host
}



// -----------------------------------------------------------------------------
//
// ldDistOpSendReceive -
//
int ldDistOpSendReceive(LdRegCacheItem*  csr,
                        CorRestVerb       verb,
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
// authorityLen - length of a URL's "scheme://host:port" prefix (before the path)
//
static int authorityLen(const char* url)
{
  const char* p = strstr(url, "://");
  if (p == NULL)
    return (int) strlen(url);
  p += 3;
  const char* slash = strchr(p, '/');
  return (slash != NULL) ? (int) (slash - url) : (int) strlen(url);
}



// -----------------------------------------------------------------------------
//
// ldDistOpEndpointIsSelf - does this endpoint point back at this broker?
//
// Compares the scheme://host:port authority of the CSR endpoint against the
// broker's own ldBrokerHttpEndpoint. A match means a forward would loop back to
// us over a socket — the self-forward short-circuit runs it in-process instead.
//
// Also used at registration time: a redirect registration has to name another
// broker (§ 12.2.2.4), and that is decided on the authority alone.
//
bool ldDistOpEndpointIsSelf(const char* endpoint)
{
  if (ldBrokerHttpEndpoint == NULL || endpoint == NULL)
    return false;

  int a = authorityLen(endpoint);
  int b = authorityLen(ldBrokerHttpEndpoint);
  return (a == b) && (strncasecmp(endpoint, ldBrokerHttpEndpoint, a) == 0);
}



// -----------------------------------------------------------------------------
//
// forwardPath - the path (+query) portion of a full forward URL
//
static const char* forwardPath(const char* url)
{
  const char* p = strstr(url, "://");
  if (p == NULL)
    return url;
  p += 3;
  const char* slash = strchr(p, '/');
  return (slash != NULL) ? slash : "/";
}






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

  uint64_t endsNs = csr->lastFailure + (uint64_t) csr->cooldownMs * 1000000ULL;

  if (nowNs >= endsNs)
    return false;

  KT_T(LdTRegMatch, "%s: matched, but NOT forwarded to: in cooldown after a failure, %llu ms left of %d",
       (csr->regId != NULL) ? csr->regId : "<no id>",
       (unsigned long long) ((endsNs - nowNs) / 1000000ULL), csr->cooldownMs);

  return true;
}



// -----------------------------------------------------------------------------
//
// distOpTraceRequest - trace an outgoing forwarded request (line / params / headers / body)
//
// Each aspect has its own trace level so they can be enabled independently.
// URL params are emitted one line per param (the '?' query string of 'url').
//
static void distOpTraceRequest(CorRestVerb verb, const char* url, CorRestKeyValue* hv, int hc, const char* body, int bodyLen)
{
  if (url == NULL)
    return;

  const char* q = strchr(url, '?');
  int         pathLen = (q != NULL) ? (int)(q - url) : (int) strlen(url);
  KT_T(LdTFwdReq, "forward request: %s %.*s", corRestVerbToString(verb), pathLen, url);

  if (q != NULL)
  {
    for (const char* p = q + 1; *p != 0; )
    {
      const char* amp = strchr(p, '&');
      int         len = (amp != NULL) ? (int)(amp - p) : (int) strlen(p);
      KT_T(LdTFwdReqParam, "forward request param: %.*s", len, p);
      if (amp == NULL) break;
      p = amp + 1;
    }
  }

  for (int i = 0; i < hc; i++)
    KT_T(LdTFwdReqHeader, "forward request header: %s: %s", hv[i].key, hv[i].value ? hv[i].value : "");

  if (body != NULL && bodyLen > 0)
    KT_T(LdTFwdReqBody, "forward request body (%d bytes): %.*s", bodyLen, bodyLen, body);
}



// -----------------------------------------------------------------------------
//
// distOpTraceResponse - trace a forwarded request's response line + headers.
// (The response body is traced by distOpBodyParse, before it is tokenized.)
//
static void distOpTraceResponse(int statusCode, CorRestKeyValue* hv, int hc)
{
  KT_T(LdTFwdRes, "forward response: status %d", statusCode);
  for (int i = 0; i < hc; i++)
    KT_T(LdTFwdResHeader, "forward response header: %s: %s", hv[i].key, hv[i].value ? hv[i].value : "");
}



int ldDistOpSendReceiveEx(LdRegCacheItem*  csr,
                          CorRestVerb       verb,
                          const char*      url,
                          const char*      body,
                          int              bodyLen,
                          const char*      ownAlias,
                          CorRestKeyValue*  extraHeaderV,
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
  CorRestKeyValue* hv = buildHeaders(verb, ownAlias, csr->tenant, csr->contextSourceInfoKV, ldDistOpForwardContext(csr), &hc);

  // Append optional extra headers (e.g. NGSILD-EntityMap for entity-map distops)
  if (extraHeaderV != NULL && extraHeaderCount > 0)
  {
    CorRestKeyValue* merged = (CorRestKeyValue*) kaAlloc(&corRest.kalloc, (hc + extraHeaderCount) * sizeof(CorRestKeyValue));
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
  resp.allocP          = &corRest.kalloc;
  resp.error           = 0;
  resp.errorDetail[0]  = 0;

  distOpTraceRequest(verb, url, hv, hc, body, req.bodyLen);

  int rc;
  if (ldDistOpEndpointIsSelf(csr->endpoint))
  {
    // Self-forward short-circuit (Inc5b): the endpoint is this broker. Run the
    // request in-process rather than opening a socket back to ourselves (which
    // stalls the epoll pool thread — the self-forward stall).
    int sc = corRestProcessInProcess(verb, forwardPath(url), hv, hc, body, req.bodyLen,
                         resp.allocP, &resp.body, &resp.bodyLen,
                         &resp.headerV, &resp.headerCount);
    if (sc < 0)
    {
      rc = -1;
      strcpy(resp.errorDetail, "in-process self-forward setup failed");
    }
    else
    {
      rc = 0;
      resp.statusCode = sc;
    }
  }
  else
    rc = plugin->send(&req, &resp);

  distOpTraceResponse(resp.statusCode, resp.headerV, resp.headerCount);

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
      char* d = (char*) kaAlloc(&corRest.kalloc, strlen(resp.errorDetail) + 1);
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
                 CorRestVerb       verb,
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
// responseContextLink - extract the json-ld#context Link URL from a response.
//
// A forwarded read body is expanded via the context that travels WITH it; for
// an application/json response that context is the URL in the response's Link
// header (rel="http://www.w3.org/ns/json-ld#context"). Returns the URL copied
// into the request kalloc, or NULL when the response carried no such Link.
//
static const char* responseContextLink(CorRestKeyValue* headerV, int headerCount)
{
  for (int h = 0; h < headerCount; h++)
  {
    if (headerV[h].key == NULL || headerV[h].value == NULL)              continue;
    if (strcasecmp(headerV[h].key, "Link") != 0)                         continue;
    if (strstr(headerV[h].value, "json-ld#context") == NULL)             continue;

    char* v = headerV[h].value;
    if (v[0] != '<')                                                     continue;
    char* end = strchr(v + 1, '>');
    if (end == NULL)                                                     continue;

    int   len = (int) (end - (v + 1));
    char* url = (char*) kaAlloc(&corRest.kalloc, len + 1);
    if (url == NULL)                                                     return NULL;
    memcpy(url, v + 1, len);
    url[len] = 0;
    return url;
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// distOpBodyParse - parse a forwarded response body once, at reception
//
// The parsed tree is cached on the result so no consumer re-parses it (kjParse
// tokenizes the buffer in place — a second parse would hit a mutated buffer).
// A 2xx response whose non-empty body will not parse is an unusable upstream
// reply: downgrade the leg to 502 Bad Gateway with a diagnostic, so every
// consumer's existing non-2xx handling treats it as a failed forward and the
// registrationId of the offending endpoint is recorded in the error.
// responseBody / responseBodyLen / statusCode must already be set on rP.
//
static void distOpBodyParse(LdDistOpBatchResult* rP)
{
  if ((rP->responseBody == NULL) || (rP->responseBodyLen == 0))
    return;

  // Trace the RAW forwarded-response body before kjParse tokenizes it in place
  // (afterwards it is no longer printable as a string).
  KT_T(LdTFwdResBody, "forward response body (status %d, %d bytes): %.*s",
       rP->statusCode, rP->responseBodyLen, rP->responseBodyLen, rP->responseBody);

  rP->responseTree = kjParse(corRest.kjsonP, rP->responseBody);

  if ((rP->responseTree == NULL) && (rP->statusCode >= 200) && (rP->statusCode < 300))
  {
    rP->statusCode  = 502;
    rP->errorDetail = "malformed JSON in forwarded response body";
  }
}



// -----------------------------------------------------------------------------
//
// ldDistOpWarnings - § 6.3.5 NGSILD-Warning for distributed GET/HEAD error legs
//
// Scans a completed forward batch and returns a request-arena string of
// comma-joined RFC 7234 [n.15] Warning warn-values, one per registered source
// that behaved abnormally while the overall response is still assembled and
// returned to the client. Each warn-value is `<code> <host[:port]> "<text>"`;
// multiple failing sources are comma-joined into a single header value. The
// § 6.3.5 warning codes emitted, by leg outcome:
//
//   199 - Miscellaneous Warning          — no response was received from the
//         endpoint within the timeout period (transport failure / timeout);
//         statusCode 0.
//   111 - Revalidation Failed            — a response arrived within the timeout
//         but its payload was invalid / non-NGSI-LD (the 2xx→502 downgrade set
//         by distOpBodyParse, errorDetail "malformed...").
//   299 - Miscellaneous Persistent Warning — a genuine HTTP error status (e.g.
//         403 - Forbidden) was received from the endpoint.
//
// NOT signalled (skipped here):
//   - a 404 Not Found — § 6.3.5 states a source responding with no data and a
//     404 is NOT abnormal behaviour for distributed operations;
//   - a cooldown-declined leg (§ 5.2.34, statusCode 504 + "cooldown" detail) —
//     no request was made at all;
//   - a 2xx/3xx success.
// (110 - Response is Stale is a GET-response-cache signal; no such cache exists.
// The 199 "registration loop detected" sub-case does not surface here: loop
// legs are reaped before forwarding — inclusive loops silently, exclusive/
// redirect loops as a hard 508 to the client, see ldDistOpLoopReap.)
//
// Returns NULL when no source qualifies.
//
char* ldDistOpWarnings(LdDistOpBatchItem* itemV, LdDistOpBatchResult* resultV, int itemCount)
{
  char* acc    = NULL;
  int   accLen = 0;

  for (int i = 0; i < itemCount; i++)
  {
    LdDistOpBatchResult* rP       = &resultV[i];
    bool                 cooldown = (rP->errorDetail != NULL) && (strstr(rP->errorDetail, "cooldown")  != NULL);
    bool                 malformed = (rP->errorDetail != NULL) && (strstr(rP->errorDetail, "malformed") != NULL);

    int         code;
    const char* warnText;

    if      (cooldown)             continue;                    // no request was made (§ 5.2.34)
    else if (rP->statusCode == 0)  { code = 199; warnText = "No response was received from the registration endpoint within the timeout period"; }
    else if (malformed)            { code = 111; warnText = "An invalid response payload was received from the registration endpoint"; }
    else if (rP->statusCode == 404) continue;                   // § 6.3.5: a 404 (no data) is NOT abnormal behaviour
    else if (rP->statusCode >= 400) { code = 299; warnText = "An error response was received from the registration endpoint"; }
    else                            continue;                   // 2xx/3xx — no abnormal behaviour

    // warn-agent — authority (host[:port]) of the registration endpoint, or "-"
    char        authority[128];
    const char* ep = ((itemV[i].csr != NULL) && (itemV[i].csr->endpoint != NULL)) ? itemV[i].csr->endpoint : NULL;

    if (ep == NULL)
      strcpy(authority, "-");
    else
    {
      if      (strncmp(ep, "https://", 8) == 0) ep += 8;
      else if (strncmp(ep, "http://",  7) == 0) ep += 7;

      int a = 0;
      while ((ep[a] != 0) && (ep[a] != '/') && (a < (int) sizeof(authority) - 1))
      {
        authority[a] = ep[a];
        a++;
      }
      authority[a] = 0;
      if (a == 0)
        strcpy(authority, "-");
    }

    char wv[256];
    int  wvLen  = snprintf(wv, sizeof(wv), "%d %s \"%s\"", code, authority, warnText);
    int  sepLen = (acc != NULL) ? 2 : 0;                       // ", " between warn-values

    char* nacc = (char*) kaAlloc(&corRest.kalloc, accLen + sepLen + wvLen + 1);
    if (nacc == NULL)
      return acc;

    if (acc != NULL)
    {
      memcpy(nacc, acc, accLen);
      memcpy(nacc + accLen, ", ", 2);
    }
    memcpy(nacc + accLen + sepLen, wv, wvLen + 1);

    acc     = nacc;
    accLen += sepLen + wvLen;
  }

  return acc;
}



// -----------------------------------------------------------------------------
//
// ldDistOpSendMulti -
//
// Fan out N CSR forwards concurrently over corRestClientMulti. The per-CSR
// timeout (§ 5.2.34) caps each request individually inside the multi engine;
// the engine itself runs with the max of all CSR timeouts so no single CSR
// stalls peers. Bypasses the LdForwardingPlugin abstraction — HTTP is the
// only transport that exists. A future non-HTTP plugin (corBin, MQTT) would
// need its own batched fan-out.
//
int ldDistOpSendMulti(LdDistOpBatchItem*     itemV,
                      int                    itemCount,
                      CorRestVerb             verb,
                      const char*            ownAlias,
                      LdDistOpBatchResult*   resultV)
{
  if (itemCount <= 0)
    return 0;

  CorRestClientMulti* multi = corRestClientMultiCreate(itemCount);
  if (multi == NULL)
  {
    for (int i = 0; i < itemCount; i++)
      resultV[i].errorDetail = "corRestClientMultiCreate failed";
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
    batchTimeoutMs = corRestClientDefaultRequestTimeoutMs;

  // Add every item. Failed adds (capacity exhausted, bad input) get
  // statusCode == 0 + errorDetail filled and skipped at perform time.
  // Self-targeted items (endpoint == this broker) are NOT added to the socket
  // multi-engine; they run in-process after the perform (addIndex == -2). Their
  // headers are built here — while corNgsild is still intact — and stashed for
  // the in-process call, because running it now would reset corNgsild mid-loop
  // and corrupt buildHeaders for the remaining socket items.
  int addedCount = 0;
  int* addIndex = (int*) kaAlloc(&corRest.kalloc, itemCount * sizeof(int));
  for (int i = 0; i < itemCount; i++) addIndex[i] = -1;
  memset(resultV, 0, itemCount * sizeof(LdDistOpBatchResult));
  CorRestKeyValue** selfHv = (CorRestKeyValue**) kaAlloc(&corRest.kalloc, itemCount * sizeof(CorRestKeyValue*));
  int*             selfHc = (int*) kaAlloc(&corRest.kalloc, itemCount * sizeof(int));

  for (int i = 0; i < itemCount; i++)
  {
    LdRegCacheItem* csr = itemV[i].csr;

    if (ldDistOpCsrInCooldown(csr))
    {
      resultV[i].statusCode  = 504;
      resultV[i].errorDetail = "registration endpoint in cooldown after failure";
      continue;
    }

    CorRestVerb itemVerb = itemV[i].hasVerb ? itemV[i].verb : verb;

    int             hc = 0;
    CorRestKeyValue* hv = buildHeaders(itemVerb, ownAlias, csr->tenant, csr->contextSourceInfoKV, ldDistOpForwardContext(csr), &hc);

    distOpTraceRequest(itemVerb, itemV[i].url, hv, hc, itemV[i].body, itemV[i].bodyLen);

    if (ldDistOpEndpointIsSelf(csr->endpoint))
    {
      selfHv[i]   = hv;
      selfHc[i]   = hc;
      addIndex[i] = -2;   // in-process; handled after perform
      continue;
    }

    int idx = corRestClientMultiAdd(multi,
                                   itemVerb,
                                   itemV[i].url,
                                   hv, hc,
                                   itemV[i].body, itemV[i].bodyLen,
                                   &corRest.kalloc,
                                   NULL);
    if (idx < 0)
    {
      resultV[i].statusCode  = 0;
      resultV[i].errorDetail = "corRestClientMultiAdd failed";
      continue;
    }

    addIndex[i] = idx;
    addedCount++;
  }

  if (addedCount > 0)
    corRestClientMultiPerform(multi, batchTimeoutMs);

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t nowNs = (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;

  for (int i = 0; i < itemCount; i++)
  {
    LdRegCacheItem* csr = itemV[i].csr;
    int             idx = addIndex[i];

    if (idx == -2)
    {
      // Self-forward: run in-process now (after the socket batch was built and
      // performed, so corNgsild was intact for the socket items' headers).
      CorRestVerb      itemVerb = itemV[i].hasVerb ? itemV[i].verb : verb;
      char*           respBody = NULL;
      int             respBodyLen = 0;
      CorRestKeyValue* respHdrV = NULL;
      int             respHdrCount = 0;

      int sc = corRestProcessInProcess(itemVerb, forwardPath(itemV[i].url), selfHv[i], selfHc[i],
                           itemV[i].body, itemV[i].bodyLen, &corRest.kalloc,
                           &respBody, &respBodyLen, &respHdrV, &respHdrCount);

      csr->timesSent++;
      if (sc < 0)
      {
        csr->timesFailed++;
        csr->lastFailure = nowNs;
        resultV[i].statusCode  = 0;
        resultV[i].errorDetail = "in-process self-forward setup failed";
        continue;
      }

      resultV[i].statusCode        = sc;
      resultV[i].responseBody      = respBody;
      resultV[i].responseBodyLen   = respBodyLen;
      distOpTraceResponse(sc, respHdrV, respHdrCount);
      distOpBodyParse(&resultV[i]);
      resultV[i].responseContextUrl = responseContextLink(respHdrV, respHdrCount);

      if (sc >= 200 && sc < 300)
        csr->lastSuccess = nowNs;
      else
      {
        csr->timesFailed++;
        csr->lastFailure = nowNs;
      }
      continue;
    }

    if (idx < 0)
    {
      // add failed — counter still ticks (we tried), errorDetail already set
      csr->timesSent++;
      csr->timesFailed++;
      csr->lastFailure = nowNs;
      continue;
    }

    CorRestClientResponse* resp = corRestClientMultiResponse(multi, idx);
    csr->timesSent++;

    if (resp == NULL || resp->error != 0)
    {
      csr->timesFailed++;
      csr->lastFailure = nowNs;
      resultV[i].statusCode = 0;
      resultV[i].timedOut   = (resp != NULL) && (resp->error == CORR_ERR_TIMEOUT);
      if (resp != NULL && resp->errorDetail[0] != 0)
      {
        char* d = (char*) kaAlloc(&corRest.kalloc, strlen(resp->errorDetail) + 1);
        strcpy(d, resp->errorDetail);
        resultV[i].errorDetail = d;
      }
      else
        resultV[i].errorDetail = "transport failure";
      continue;
    }

    // resp->body points into the multi engine's per-connection read buffer,
    // which corRestClientMultiDestroy frees. Copy into the request kalloc so
    // the caller can keep using it after destroy.
    resultV[i].statusCode      = resp->statusCode;
    resultV[i].responseBodyLen = resp->bodyLen;
    if (resp->body != NULL && resp->bodyLen > 0)
    {
      char* bodyCopy = (char*) kaAlloc(&corRest.kalloc, resp->bodyLen + 1);
      memcpy(bodyCopy, resp->body, resp->bodyLen);
      bodyCopy[resp->bodyLen] = 0;
      resultV[i].responseBody = bodyCopy;
    }
    else
      resultV[i].responseBody = NULL;

    distOpTraceResponse(resp->statusCode, resp->headerV, resp->headerCount);
    distOpBodyParse(&resultV[i]);

    // The context the response speaks (its json-ld#context Link), so the
    // caller can expand the body via its own vocabulary, not the request's.
    resultV[i].responseContextUrl = responseContextLink(resp->headerV, resp->headerCount);

    if (resp->statusCode >= 200 && resp->statusCode < 300)
      csr->lastSuccess = nowNs;
    else
    {
      csr->timesFailed++;
      csr->lastFailure = nowNs;
    }
  }

  corRestClientMultiDestroy(multi);
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

  KjNode* entry = kjObject(corRest.kjsonP, NULL);
  kjChildAdd(entry, kjString(corRest.kjsonP, "entityId", entityId));

  KjNode* pd = kjObject(corRest.kjsonP, "error");
  kjChildAdd(pd, kjString (corRest.kjsonP, "type",   errorType));
  kjChildAdd(pd, kjString (corRest.kjsonP, "title",  errorTitle));
  kjChildAdd(pd, kjInteger(corRest.kjsonP, "status", statusCode));
  kjChildAdd(pd, kjString (corRest.kjsonP, "detail", errorDetail));
  kjChildAdd(entry, pd);

  if (regId != NULL)
    kjChildAdd(entry, kjString(corRest.kjsonP, "registrationId", regId));

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

  KjNode* pd = kjObject(corRest.kjsonP, NULL);

  // type / title — clone from the first error so the body looks like a
  // standalone ProblemDetails. Drop "detail" (the per-entity detail strings
  // become noise once we collapse — title alone is clear).
  KjNode* typeP  = kjLookup(errP, "type");
  KjNode* titleP = kjLookup(errP, "title");
  if (typeP  != NULL) kjChildAdd(pd, kjClone(corRest.kjsonP, typeP));
  if (titleP != NULL) kjChildAdd(pd, kjClone(corRest.kjsonP, titleP));

  // entityId(s) — extension field (anticipated ETSI ProblemDetails extension).
  int n = 0;
  for (KjNode* e = errorsArrayP->value.firstChildP; e != NULL; e = e->next) n++;

  if (n == 1)
  {
    KjNode* idP = kjLookup(first, "entityId");
    if (idP != NULL && idP->type == KjString)
      kjChildAdd(pd, kjString(corRest.kjsonP, "entityId", idP->value.s));
  }
  else
  {
    KjNode* idsArr = kjArray(corRest.kjsonP, "entityIds");
    for (KjNode* e = errorsArrayP->value.firstChildP; e != NULL; e = e->next)
    {
      KjNode* idP = kjLookup(e, "entityId");
      if (idP != NULL && idP->type == KjString)
        kjChildAdd(idsArr, kjString(corRest.kjsonP, NULL, idP->value.s));
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
  if (riP->attributeNamesV == NULL) return true;
  for (int i = 0; riP->attributeNamesV[i] != NULL; i++)
    if (strcmp(riP->attributeNamesV[i], attrIri) == 0) return true;
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
    entries = (LdDistOpEntry*) kaAlloc(&corRest.kalloc, capacity * sizeof(LdDistOpEntry));
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

      if (!ldRegOpSupported(csr, op))
      {
        KT_T(LdTRegMatch, "%s: matched, but NOT forwarded to: the registration's 'operations' does not cover %s",
             (csr->regId != NULL) ? csr->regId : "<no id>", opName);

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

      // A forward to this CSR loops when our own alias is already in the
      // inbound Via (§ 9.7: we are a previously-encountered source) or the CSR
      // resolves back to us / a transited broker. We no longer silently skip
      // such CSRs: the entry is built and marked so the caller can turn a
      // loop-blocked exclusive/redirect forward into 508 (§ 6.3.18). Inclusive
      // loops stay silent — the local copy serves them.
      bool loop = ldDistOpLoopDetected(ownAlias) || ldDistOpCsrWouldLoop(csr, ownAlias);

      if (!perRi)
      {
        entries[count].csr       = csr;
        entries[count].riP       = NULL;
        entries[count].modeIdx   = g;
        entries[count].wouldLoop = loop;
        entries[count].errorMode = grp->opConflict;
        count++;
        continue;
      }

      for (LdRegInfo* riP = csr->infoV; riP != NULL; riP = riP->next)
      {
        if (riEntityIdCheck != NULL && !ldoEntityInfoCoversId(riP, riEntityIdCheck)) continue;
        if (riAttrIriCheck  != NULL && !ldoInfoCoversAttr(riP, riAttrIriCheck))     continue;

        entries[count].csr       = csr;
        entries[count].riP       = riP;
        entries[count].modeIdx   = g;
        entries[count].wouldLoop = loop;
        entries[count].errorMode = grp->opConflict;
        count++;
      }
    }
  }

  *entriesPP = entries;
  return count;
}



// -----------------------------------------------------------------------------
//
// ldDistOpLoopReap -
//
int ldDistOpLoopReap(LdDistOpEntry* entries, int count)
{
  int kept = 0;

  for (int i = 0; i < count; i++)
  {
    if (entries[i].wouldLoop)
    {
      // exclusive/redirect (errorMode) loop-block means the data is held
      // externally and is now unreachable. Flag it so ldRenderHook turns the
      // caller's terminal 404 into 508 (§ 6.3.18). Inclusive loop-blocks are
      // silent: the local copy still serves them.
      if (entries[i].errorMode)
        corNgsild.loopBlocked508 = true;
      continue;   // loop-blocked — never forwarded
    }

    entries[kept++] = entries[i];
  }

  return kept;
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
                            CorRestVerb     verb,
                            const char*    ownAlias)
{
  if (count <= 0) return;

  LdDistOpBatchItem*   items   = (LdDistOpBatchItem*)   kaAlloc(&corRest.kalloc, count * sizeof(LdDistOpBatchItem));
  LdDistOpBatchResult* results = (LdDistOpBatchResult*) kaAlloc(&corRest.kalloc, count * sizeof(LdDistOpBatchResult));
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
    entries[i].timedOut        = results[i].timedOut;
  }
}
