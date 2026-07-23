//
// FILE            ldProbeSourceIdentity.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//

#include <stdlib.h>                                     // malloc, free, strdup
#include <stdio.h>                                      // snprintf
#include <string.h>                                     // strlen, strcpy

#include "kalloc/KAlloc.h"                              // KAlloc
#include "kalloc/kaBufferInit.h"                        // kaBufferInit
#include "kalloc/kaBufferReset.h"                       // kaBufferReset
#include "kjson/kjson.h"                                // Kjson
#include "kjson/kjBufferCreate.h"                       // kjBufferCreate
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjParse.h"                              // kjParse
#include "kjson/kjLookup.h"                             // kjLookup
#include "swRest/SwRestKeyValue.h"                      // SwRestKeyValue
#include "swRest/SwRestVerb.h"                          // SwVerbGet

#include "swNgsild/LdForwarding.h"                      // LdForwardRequest, LdForwardResponse, LdForwardingPlugin
#include "swNgsild/ldForwarding.h"                      // ldForwardingForEndpoint
#include "swNgsild/ldStripAtContext.h"                  // ldStripAtContext

#include "swNgsild/ldProbeSourceIdentity.h"             // Own interface



// Default probe timeout if caller passes 0 — short so it doesn't stall
// the CSR-create request path noticeably.
#define LD_PROBE_TIMEOUT_DEFAULT_MS  500



// -----------------------------------------------------------------------------
//
// ldProbeSourceIdentity -
//
char* ldProbeSourceIdentity(const char* endpoint, const char* tenant, int timeoutMs)
{
  if (endpoint == NULL || endpoint[0] == 0)
    return NULL;

  const LdForwardingPlugin* plugin = ldForwardingForEndpoint(endpoint);
  if (plugin == NULL)
    return NULL;

  //
  // Compose URL: <endpoint>/ngsi-ld/v1/info/sourceIdentity
  // The sourceIdentity resource (§ 12.2.2) is defined relative to the API root
  // {apiRoot}/ngsi-ld/v1 (§ 5), so the full path carries the /ngsi-ld/v1 prefix.
  //
  static const char pathSuffix[] = "/ngsi-ld/v1/info/sourceIdentity";
  int   endpointLen = strlen(endpoint);
  char  urlBuf[1024];
  if (endpointLen + sizeof(pathSuffix) > sizeof(urlBuf))
    return NULL;
  memcpy(urlBuf, endpoint, endpointLen);
  memcpy(urlBuf + endpointLen, pathSuffix, sizeof(pathSuffix));

  //
  // Headers: NGSILD-Tenant (if non-default) so the response's alias is
  // the tenant-scoped one we care about.
  //
  SwRestKeyValue headerV[2];
  int            headerCount = 0;
  if (tenant != NULL && tenant[0] != 0)
  {
    headerV[headerCount].key   = (char*) "NGSILD-Tenant";
    headerV[headerCount].value = (char*) tenant;
    headerCount++;
  }

  //
  // Scratch kalloc for the forwarding-plugin response body. Stack-
  // allocated buffer keeps the probe cheap and avoids malloc churn.
  //
  char    allocBuffer[4096];
  KAlloc  scratchKa;
  kaBufferInit(&scratchKa, allocBuffer, sizeof(allocBuffer), 16384, NULL, "probe-sourceIdentity");

  LdForwardRequest  req;
  LdForwardResponse resp;

  req.endpoint         = urlBuf;
  req.verb             = SwVerbGet;
  req.headerV          = (headerCount > 0) ? headerV : NULL;
  req.headerCount      = headerCount;
  req.body             = NULL;
  req.bodyLen          = 0;
  // Bound the CONNECT leg too — connectTimeoutMs 0 falls back to the
  // client's 5-second default, which is exactly the stall the probe's
  // short request timeout is meant to avoid (an unreachable registered
  // endpoint made every CSR create block 5s; 80+ creates per ETSI run).
  req.connectTimeoutMs = (timeoutMs > 0) ? timeoutMs : LD_PROBE_TIMEOUT_DEFAULT_MS;
  req.requestTimeoutMs = (timeoutMs > 0) ? timeoutMs : LD_PROBE_TIMEOUT_DEFAULT_MS;

  resp.statusCode     = 0;
  resp.headerV        = NULL;
  resp.headerCount    = 0;
  resp.body           = NULL;
  resp.bodyLen        = 0;
  resp.allocP         = &scratchKa;
  resp.error          = 0;
  resp.errorDetail[0] = 0;

  int rc = plugin->send(&req, &resp);
  if (rc != 0)
  {
    kaBufferReset(&scratchKa, 0);
    return NULL;
  }

  if (resp.statusCode < 200 || resp.statusCode >= 300 || resp.body == NULL || resp.bodyLen == 0)
  {
    kaBufferReset(&scratchKa, 0);
    return NULL;
  }

  //
  // Parse the body and extract contextSourceAlias.
  //
  Kjson   kjsonLocal;
  Kjson*  kjsonP = kjBufferCreate(&kjsonLocal, &scratchKa);
  KjNode* treeP  = kjParse(kjsonP, resp.body);
  if (treeP == NULL)
  {
    kaBufferReset(&scratchKa, 0);
    return NULL;
  }

  ldStripAtContext(treeP);

  KjNode* aliasP = kjLookup(treeP, "contextSourceAlias");
  if (aliasP == NULL || aliasP->type != KjString || aliasP->value.s == NULL)
  {
    kaBufferReset(&scratchKa, 0);
    return NULL;
  }

  //
  // strdup the alias (into process heap) before tearing down the
  // scratch arena — the caller owns the string.
  //
  char* result = strdup(aliasP->value.s);
  kaBufferReset(&scratchKa, 0);
  return result;
}
