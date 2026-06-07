//
// FILE            ldContextHost.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                   // bool
#include <stdint.h>                                    // uint64_t
#include <string.h>                                    // memcpy, strlen
#include <stdio.h>                                     // snprintf
#include <time.h>                                      // time

#include "kalloc/KAlloc.h"                             // KAlloc, kaAlloc
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjRenderSize.h"                        // kjFastRenderSize
#include "kjson/kjRender.h"                            // kjFastRender

#include "swJsonld/SwldContext.h"                      // SwldContext, SwldKindImplicit
#include "swJsonld/SwldContextCache.h"                 // SwldContextCache
#include "swJsonld/swldCache.h"                        // swldCacheInsert, swldCacheReapVolatile
#include "swJsonld/swldContextParse.h"                 // swldContextFromObject, swldContextFromTree
#include "swJsonld/swldIdGen.h"                        // swldIdGenerate

#include "swNgsild/SwNgsild.h"                         // ldBrokerHttpEndpoint
#include "swNgsild/ldPeriodicLoop.h"                   // ldPeriodicLoopRegister
#include "swNgsild/ldContextHost.h"                    // Own interface

extern SwldContextCache* swldCacheGet(void);



// -----------------------------------------------------------------------------
//
// ldContextHostVolatile - see header.
//
SwldContext* ldContextHostVolatile(KjNode* ctxBody)
{
  if (ctxBody == NULL)
    return NULL;
  if (ctxBody->type != KjObject && ctxBody->type != KjArray)
    return NULL;

  SwldContextCache* cacheP = swldCacheGet();
  KAlloc*           storeP = (cacheP != NULL) ? cacheP->kaP : NULL;
  if (storeP == NULL)
    return NULL;

  char* id = swldIdGenerate(storeP);
  if (id == NULL)
    return NULL;

  // Build the expansion / compaction maps from the body — same parse the
  // sub/reg auto-population uses.
  SwldContext* ctxP = (ctxBody->type == KjObject)
                        ? swldContextFromObject(ctxBody, storeP, NULL)
                        : swldContextFromTree(ctxBody, storeP);
  if (ctxP == NULL)
    return NULL;

  // Persist the wrapper {"@context": <orig>} as the served body so a GET of
  // the served URL returns a self-contained context document.
  int   bodyLen = kjFastRenderSize(ctxBody) + 32;
  char* bodyBuf = (char*) kaAlloc(storeP, bodyLen);
  if (bodyBuf == NULL)
    return NULL;

  int p = 0;
  p += snprintf(bodyBuf + p, bodyLen - p, "{\"@context\":");
  kjFastRender(ctxBody, bodyBuf + p);
  p += strlen(bodyBuf + p);
  snprintf(bodyBuf + p, bodyLen - p, "}");

  // Served URL = <httpEndpoint>/ngsi-ld/v1/jsonldContexts/{id}. Set it on
  // the context itself so emission sites (Link header) read ->url directly,
  // unlike the sub/reg path which keeps url=NULL and synthesizes on list.
  const char* prefix  = "/ngsi-ld/v1/jsonldContexts/";
  const char* base    = (ldBrokerHttpEndpoint != NULL) ? ldBrokerHttpEndpoint : "";
  int         baseLen = strlen(base);
  int         prefLen = strlen(prefix);
  int         idLen   = strlen(id);
  char*       urlBuf  = (char*) kaAlloc(storeP, baseLen + prefLen + idLen + 1);
  if (urlBuf == NULL)
    return NULL;
  memcpy(urlBuf, base, baseLen);
  memcpy(urlBuf + baseLen, prefix, prefLen);
  memcpy(urlBuf + baseLen + prefLen, id, idLen);
  urlBuf[baseLen + prefLen + idLen] = 0;

  ctxP->id          = id;
  ctxP->url         = urlBuf;
  ctxP->body        = bodyBuf;
  ctxP->kind        = SwldKindImplicit;
  ctxP->volatileCtx = true;
  ctxP->expiresAt   = (double) time(NULL) + LD_VOLATILE_CTX_TTL_SEC;

  swldCacheInsert(ctxP);   // cache only — never db.contextSave

  return ctxP;
}



// -----------------------------------------------------------------------------
//
// volatileReapTick - periodic backstop for never-fetched volatile contexts
//
static void volatileReapTick(void* ctx, uint64_t nowNs, KAlloc* kaP)
{
  (void) ctx;
  (void) nowNs;
  (void) kaP;

  swldCacheReapVolatile((double) time(NULL));
}



// -----------------------------------------------------------------------------
//
// ldContextHostReaperStart - see header.
//
void ldContextHostReaperStart(void)
{
  ldPeriodicLoopRegister(volatileReapTick, NULL);
}
