//
// FILE            ldContextHost.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                   // bool
#include <stdint.h>                                    // uint64_t
#include <string.h>                                    // memcpy, strlen
#include <stdio.h>                                     // snprintf
#include <time.h>                                      // time

#include "kalloc/KAlloc.h"                             // KAlloc, kaAlloc
#include "kalloc/kaStrdup.h"                           // kaStrdup
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjRenderSize.h"                        // kjFastRenderSize
#include "kjson/kjRender.h"                            // kjFastRender

#include "corRest/CorRestState.h"                        // corRest (per-request scratch allocator)

#include "corJsonld/CorLdContext.h"                      // CorLdContext, CorLdKindImplicit
#include "corJsonld/CorLdContextCache.h"                 // CorLdContextCache
#include "corJsonld/corLdCache.h"                        // corLdCacheLookup, corLdCacheInsert, corLdCacheReapVolatile
#include "corJsonld/corLdContextParse.h"                 // corLdContextFromObject, corLdContextFromTree

#include "corNgsild/CorNgsild.h"                         // ldBrokerHttpEndpoint
#include "corNgsild/ldPeriodicLoop.h"                   // ldPeriodicLoopRegister
#include "corNgsild/ldContextHost.h"                    // Own interface

extern CorLdContextCache* corLdCacheGet(void);



// -----------------------------------------------------------------------------
//
// fnv1a64 - FNV-1a 64-bit hash, for content-addressing the served body.
//
static uint64_t fnv1a64(const char* s, int len)
{
  uint64_t h = 1469598103934665603ULL;

  for (int i = 0; i < len; i++)
  {
    h ^= (unsigned char) s[i];
    h *= 1099511628211ULL;
  }

  return h;
}



// -----------------------------------------------------------------------------
//
// ldContextHostVolatile - see header.
//
// Content-addressed + deduplicated: the served context's id is a hash of its
// body, so identical inline @contexts (the common case — a client app sends
// the same one on every request) collapse to ONE cache entry no matter the
// request rate. Each call refreshes the TTL (sliding), so the entry lives as
// long as it keeps being used and is reaped TTL seconds after the last use.
// The cache is therefore bounded by the number of DISTINCT inline contexts
// seen in a TTL window, not by request count.
//
CorLdContext* ldContextHostVolatile(KjNode* ctxBody)
{
  if (ctxBody == NULL)
    return NULL;
  if (ctxBody->type != KjObject && ctxBody->type != KjArray)
    return NULL;

  CorLdContextCache* cacheP = corLdCacheGet();
  KAlloc*           storeP = (cacheP != NULL) ? cacheP->kaP : NULL;
  if (storeP == NULL)
    return NULL;

  // Render the wrapper {"@context": <orig>} into the PER-REQUEST scratch
  // allocator (not the cache arena): on a dedup hit we discard it with the
  // request and never touch the arena — so repeated identical contexts under
  // load don't grow the cache allocator at all.
  int   bodyLen = kjFastRenderSize(ctxBody) + 32;
  char* scratch = (char*) kaAlloc(&corRest.kalloc, bodyLen);
  if (scratch == NULL)
    return NULL;

  int p = 0;
  p += snprintf(scratch + p, bodyLen - p, "{\"@context\":");
  kjFastRender(ctxBody, scratch + p);
  p += strlen(scratch + p);
  p += snprintf(scratch + p, bodyLen - p, "}");

  // id = urn:ngsi-ld:Context:v<hash>. The 'v' marks a content-addressed
  // volatile context, distinct from the sub/reg counter-based ids.
  char id[64];
  snprintf(id, sizeof(id), "urn:ngsi-ld:Context:v%016llx",
           (unsigned long long) fnv1a64(scratch, p));

  double now = (double) time(NULL);

  // Dedup: an identical body already hosted? Refresh its TTL and reuse it.
  // The body-equality guard makes a (astronomically unlikely) hash collision
  // a correctness non-event — a mismatch just falls through to re-host.
  CorLdContext* hitP = corLdCacheLookup(id);
  if (hitP != NULL && hitP->volatileCtx && hitP->body != NULL &&
      strcmp(hitP->body, scratch) == 0)
  {
    hitP->expiresAt = now + LD_VOLATILE_CTX_TTL_SEC;
    return hitP;
  }

  // Miss — build the entry. Copy id + body into the cache arena (they must
  // outlive the request); the maps come from the same parse the sub/reg
  // auto-population uses.
  CorLdContext* ctxP = (ctxBody->type == KjObject)
                        ? corLdContextFromObject(ctxBody, storeP, NULL)
                        : corLdContextFromTree(ctxBody, storeP, NULL);  // Hosted @context - identified by localId, no URL of its own to resolve against
  if (ctxP == NULL)
    return NULL;

  char* idBuf  = kaStrdup(storeP, id);
  char* bodyP  = kaStrdup(storeP, scratch);
  if (idBuf == NULL || bodyP == NULL)
    return NULL;

  // Served URL = <httpEndpoint>/ngsi-ld/v1/jsonldContexts/{id}. Set it on
  // the context itself so emission sites (Link header) read ->url directly,
  // unlike the sub/reg path which keeps url=NULL and synthesizes on list.
  const char* prefix  = "/ngsi-ld/v1/jsonldContexts/";
  const char* base    = (ldBrokerHttpEndpoint != NULL) ? ldBrokerHttpEndpoint : "";
  int         baseLen = strlen(base);
  int         prefLen = strlen(prefix);
  int         idLen   = strlen(idBuf);
  char*       urlBuf  = (char*) kaAlloc(storeP, baseLen + prefLen + idLen + 1);
  if (urlBuf == NULL)
    return NULL;
  memcpy(urlBuf, base, baseLen);
  memcpy(urlBuf + baseLen, prefix, prefLen);
  memcpy(urlBuf + baseLen + prefLen, idBuf, idLen);
  urlBuf[baseLen + prefLen + idLen] = 0;

  ctxP->id          = idBuf;
  ctxP->url         = urlBuf;
  ctxP->body        = bodyP;
  ctxP->kind        = CorLdKindImplicit;
  ctxP->volatileCtx = true;
  ctxP->expiresAt   = now + LD_VOLATILE_CTX_TTL_SEC;

  corLdCacheInsert(ctxP);   // cache only — never db.contextSave

  return ctxP;
}



// -----------------------------------------------------------------------------
//
// volatileReapTick - periodic backstop: drop volatile contexts TTL seconds
// after their last use.
//
static void volatileReapTick(void* ctx, uint64_t nowNs, KAlloc* kaP)
{
  (void) ctx;
  (void) nowNs;
  (void) kaP;

  corLdCacheReapVolatile((double) time(NULL));
}



// -----------------------------------------------------------------------------
//
// ldContextHostReaperStart - see header.
//
void ldContextHostReaperStart(void)
{
  ldPeriodicLoopRegister(volatileReapTick, NULL);
}
