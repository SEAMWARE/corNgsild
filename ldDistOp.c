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

#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjBuilder.h"                           // kjObject, kjString, kjChildAdd

#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "swRest/SwRestState.h"                        // swRest
#include "swRest/SwRestKeyValue.h"                     // SwRestKeyValue
#include "swRest/SwRestVerb.h"                         // SwVerbGet, SwVerbDelete

#include "swNgsild/LdForwarding.h"                     // LdForwardRequest, LdForwardResponse, LdForwardingPlugin
#include "swNgsild/ldForwarding.h"                     // ldForwardingForEndpoint
#include "swNgsild/LdRegCache.h"                       // LdRegCacheItem
#include "swNgsild/ldCsourceAlias.h"                   // ldViaHasAlias
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
//   * Content-Type: defaults to "application/ld+json" for body-carrying
//     verbs, overridden by csi.contentType if present; suppressed for
//     GET / DELETE.
//   * Accept: emitted only if csi.accept is present.
//   * Via: every incoming Via pass-through, then own-alias as final hop.
//   * NGSILD-Tenant: csr->tenant if set (§ 5.2.9).
//   * Arbitrary csi entries appended verbatim, minus the banned set
//     (§ 4.3.6.5: Content-Length, Host, NGSILD-Tenant, and the TODO
//     keys jsonldContext + ngsildConformance).
//
static SwRestKeyValue* buildHeaders(SwRestVerb   verb,
                                    const char*  ownAlias,
                                    const char*  csrTenant,
                                    char**       csrInfoKV,
                                    int*         hcP)
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

  int cap = viaIn + 4 + csiCount;   // Content-Type + Accept + own Via + NGSILD-Tenant + info[]
  SwRestKeyValue* hv = (SwRestKeyValue*) kaAlloc(&swRest.kalloc, cap * sizeof(SwRestKeyValue));
  int hc = 0;

  const char* csiContentType = NULL;
  const char* csiAccept      = NULL;
  if (csrInfoKV != NULL)
  {
    for (int i = 0; csrInfoKV[i] != NULL; i += 2)
    {
      const char* k = csrInfoKV[i];
      if      (strcasecmp(k, "contentType") == 0)  csiContentType = csrInfoKV[i + 1];
      else if (strcasecmp(k, "accept")      == 0)  csiAccept      = csrInfoKV[i + 1];
    }
  }

  if (wantBody)
  {
    hv[hc].key   = (char*) "Content-Type";
    hv[hc].value = (char*) (csiContentType != NULL ? csiContentType : "application/ld+json");
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
      if (strcasecmp(k, "jsonldContext")     == 0) continue;   // TODO
      if (strcasecmp(k, "ngsildConformance") == 0) continue;   // TODO

      hv[hc].key   = (char*) k;
      hv[hc].value = csrInfoKV[i + 1];
      hc++;
    }
  }

  *hcP = hc;
  return hv;
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
  SwRestKeyValue* hv = buildHeaders(verb, ownAlias, csr->tenant, csr->contextSourceInfoKV, &hc);

  LdForwardRequest  req;
  LdForwardResponse resp;

  req.endpoint         = url;
  req.verb             = verb;
  req.headerV          = hv;
  req.headerCount      = hc;
  req.body             = body;
  req.bodyLen          = (body != NULL) ? bodyLen : 0;
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
// ldDistOpBatchErrorAdd -
//
void ldDistOpBatchErrorAdd(KjNode*      errorsArrayP,
                           const char*  entityId,
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
  kjChildAdd(pd, kjString(swRest.kjsonP, "type",   errorType));
  kjChildAdd(pd, kjString(swRest.kjsonP, "title",  errorTitle));
  kjChildAdd(pd, kjString(swRest.kjsonP, "detail", errorDetail));
  kjChildAdd(entry, pd);

  if (regId != NULL)
    kjChildAdd(entry, kjString(swRest.kjsonP, "registrationId", regId));

  kjChildAdd(errorsArrayP, entry);
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
