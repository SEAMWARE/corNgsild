//
// FILE            ldPernotLoop.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                   // bool
#include <stdint.h>                                    // uint64_t
#include <string.h>                                    // strcmp, strlen, strcpy
#include <stdio.h>                                     // snprintf
#include <time.h>                                      // clock_gettime
#include <stdlib.h>                                    // malloc, free

#include "kalloc/KAlloc.h"                             // KAlloc, kaAlloc
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjBuilder.h"                           // kjObject, kjString, kjArray, kjChildAdd
#include "kjson/kjRenderSize.h"                        // kjFastRenderSize
#include "kjson/kjRender.h"                            // kjFastRender
#include "kjson/kjClone.h"                             // kjClone
#include "kjson/kjBufferCreate.h"                      // kjBufferCreate (+ Kjson type)
#include "kjson/kjLookup.h"                            // kjLookup

#include "corRest/CorRestState.h"                        // corRest (for thread-local init)
#include "corRest/corRestClient.h"                       // CorRestClientRequest, corRestClientSend

#include "corJsonld/corLdCompactTree.h"                  // corLdCompactTree, corLdCompactTreeWith
#include "corJsonld/corLdDownload.h"                     // corLdContextFromUrl
#include "corJsonld/corLdInit.h"                         // corLdCoreContext, CorLdContext
#include "corJsonld/CorLdContext.h"                      // CorLdContext

#include "corNgsild/CorNgsild.h"                         // ldDefaultCooldownNs
#include "corNgsild/LdVocab.h"                          // LD_VOCAB_*
#include "corNgsild/LdPernotCache.h"                    // LdPernotCache, LdPernotItem
#include "corNgsild/ldLinkedEntitiesHook.h"             // ldLinkedEntitiesHookInvoke
#include "corNgsild/ldEntityToApi.h"                    // ldEntityToApi
#include "corNgsild/ldStripSysAttrs.h"                  // ldStripSysAttrs
#include "corNgsild/ldPeriodicLoop.h"                   // ldPeriodicLoopRegister
#include "corNgsild/ldPernotLoop.h"                     // Own interface



static LdPernotCache*      loopCache   = NULL;
static LdPernotQueryFunc   loopQueryFn = NULL;



// -----------------------------------------------------------------------------
//
// nowNanos -
//
static uint64_t nowNanos(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}



// -----------------------------------------------------------------------------
//
// isoFromNanos -
//
static void isoFromNanos(uint64_t ns, char* buf, int bufLen)
{
  time_t secs = (time_t)(ns / 1000000000ULL);
  int    ms   = (int)((ns % 1000000000ULL) / 1000000ULL);
  struct tm tm;

  gmtime_r(&secs, &tm);
  snprintf(buf, bufLen, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
           tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
}



// -----------------------------------------------------------------------------
//
// pernotSendNotification - build + send a periodic notification for one sub
//
static bool pernotSendNotification(LdPernotItem* itemP, KjNode* entityArray, KAlloc* kaP)
{
  if (itemP->endpointUri == NULL)
    return false;

  // Build the notification tree in the per-tick engine arena (kaP) — this runs
  // on the periodic-dispatch thread where corRest.kjsonP is not our arena. kaP is
  // reset by the engine after the tick, freeing the whole tree.
  Kjson   kjBuf;
  Kjson*  kjP = kjBufferCreate(&kjBuf, kaP);

  // Build notification tree
  KjNode* notification = kjObject(kjP, NULL);

  char notifId[80];
  snprintf(notifId, sizeof(notifId), "urn:ngsi-ld:Notification:%08x:%04x",
           (unsigned)(nowNanos() >> 16), (unsigned)(nowNanos() & 0xFFFF));

  char isoTimeBuf[64];
  isoFromNanos(nowNanos(), isoTimeBuf, sizeof(isoTimeBuf));

  kjChildAdd(notification, kjString(kjP, "id", notifId));
  kjChildAdd(notification, kjString(kjP, "type", "Notification"));
  kjChildAdd(notification, kjString(kjP, "subscriptionId", itemP->subId));
  kjChildAdd(notification, kjString(kjP, "notifiedAt", isoTimeBuf));

  // Convert entities from storage to API format
  KjNode* dataArray = kjArray(kjP, "data");
  for (KjNode* entityP = entityArray->value.firstChildP; entityP != NULL; entityP = entityP->next)
  {
    KjNode* entityClone = kjClone(kjP, entityP);
    ldEntityToApi(entityClone, kaP);
    ldStripSysAttrs(entityClone);
    kjChildAdd(dataArray, entityClone);
  }
  kjChildAdd(notification, dataArray);

  // § 5.2.14 notification.join — linked-entity retrieval (§ 4.5.23)
  if (itemP->notifJoin != NULL && strcmp(itemP->notifJoin, "@none") != 0)
  {
    int level = (itemP->notifJoinLevel > 0) ? itemP->notifJoinLevel : 1;
    ldLinkedEntitiesHookInvoke(dataArray, itemP->notifJoin, level, false, itemP->tenantP);
  }

  // Compact using the subscription's @context so attribute IRIs (e.g.
  // `https://ngsi-ld-test-suite/context#airQualityLevel`) come back as
  // their short names — otherwise notifications ship expanded IRIs as
  // keys (046_02_01). Falls back to core if the URL can't be resolved.
  {
    CorLdContext* notifCtx = NULL;
    if (itemP->contextUrl != NULL)
      notifCtx = corLdContextFromUrl(itemP->contextUrl, kaP);
    if (notifCtx != NULL)
      corLdCompactTreeWith(notification, notifCtx);
    else
      corLdCompactTree(notification);
  }

  // Render to JSON
  int bodySize = kjFastRenderSize(notification);
  char* body = (char*) kaAlloc(kaP, bodySize);
  kjFastRender(notification, body);

  // Send via HTTP
  CorRestClientRequest  req;
  CorRestClientResponse resp;

  corRestClientRequestInit(&req, CorVerbPost, itemP->endpointUri, kaP);
  corRestClientRequestHeader(&req, "Content-Type", "application/json");

  // Link header with @context
  CorLdContext* ctxP = corLdCoreContext();
  if (ctxP != NULL && ctxP->url != NULL)
  {
    char linkBuf[512];
    snprintf(linkBuf, sizeof(linkBuf),
             "<%s>; rel=\"http://www.w3.org/ns/json-ld#context\"; type=\"application/ld+json\"",
             itemP->contextUrl ? itemP->contextUrl : ctxP->url);
    corRestClientRequestHeader(&req, "Link", linkBuf);
  }

  // § 5.2.15 endpoint.receiverInfo — emit each {key,value} as a request header.
  // Periodic notifications run on a background thread with no triggering
  // request, so any "urn:ngsi-ld:request" placeholder is silently dropped.
  if (itemP->receiverInfo != NULL && itemP->receiverInfo->type == KjArray)
  {
    for (KjNode* kvP = itemP->receiverInfo->value.firstChildP; kvP != NULL; kvP = kvP->next)
    {
      if (kvP->type != KjObject) continue;
      KjNode* kP = kjLookup(kvP, "key");
      KjNode* vP = kjLookup(kvP, "value");
      if (kP == NULL || kP->type != KjString || vP == NULL || vP->type != KjString) continue;
      if (strcmp(vP->value.s, "urn:ngsi-ld:request") == 0) continue;
      corRestClientRequestHeader(&req, kP->value.s, vP->value.s);
    }
  }

  corRestClientRequestBody(&req, body, strlen(body));
  // § 5.2.15 endpoint.timeout — per-sub override; default 10s
  int reqTmoMs = (itemP->timeoutMs > 0) ? itemP->timeoutMs : 10000;
  corRestClientRequestTimeout(&req, 5000, reqTmoMs);

  int rc = corRestClientSend(&req, &resp);
  corRestClientResponseCleanup(&resp);

  if (rc == 0 && resp.statusCode >= 200 && resp.statusCode < 300)
    return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// pernotTick - registered with the periodic-dispatch engine; called
// once per second. Walks the LdPernotCache and fires any due item.
//
// `ctx` is the LdPernotCache* passed at registration time. `kaP` is
// scratch from the engine; reset before every consumer's tick call.
//
static void pernotTick(void* ctx, uint64_t now, KAlloc* kaP)
{
  LdPernotCache* cacheP = (LdPernotCache*) ctx;
  if (cacheP == NULL || loopQueryFn == NULL) return;

  for (LdPernotItem* itemP = cacheP->head; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->state == LdPernotPaused || itemP->state == LdPernotExpired)
      continue;

    if (itemP->expiresAt > 0 && itemP->expiresAt <= now)
    {
      itemP->state = LdPernotExpired;
      continue;
    }

    if (itemP->state == LdPernotErroneous)
    {
      // § 5.2.15 endpoint.cooldown — minimum delay before retrying after
      // a failure. Default 30s when unspecified.
      uint64_t cool = (itemP->cooldownNs != 0) ? itemP->cooldownNs : ldDefaultCooldownNs;
      if (itemP->lastFailure + cool > now)
        continue;
      itemP->state = LdPernotActive;
    }

    uint64_t intervalNs = (uint64_t) itemP->timeInterval * 1000000000ULL;
    if (itemP->lastNotification + intervalNs > now)
      continue;

    KjNode* entityArray = loopQueryFn(itemP->tenantP, itemP, kaP);

    itemP->lastNotification = now;
    itemP->timesSent++;

    if (entityArray == NULL || entityArray->value.firstChildP == NULL)
    {
      itemP->noMatch++;
      continue;
    }

    bool ok = pernotSendNotification(itemP, entityArray, kaP);

    if (ok)
    {
      itemP->lastSuccess = now;
      itemP->consecutiveErrors = 0;
    }
    else
    {
      itemP->timesFailed++;
      itemP->lastFailure = now;
      itemP->consecutiveErrors++;
      if (itemP->consecutiveErrors >= 3)
        itemP->state = LdPernotErroneous;
    }
  }
}



// -----------------------------------------------------------------------------
//
// ldPernotLoopStart / ldPernotLoopStop -
//
// Now thin wrappers: register the pernot tick with the shared engine,
// store the cacheP + query callback. The thread itself comes from the
// engine, started by the broker app once at boot via ldPeriodicLoopStart.
//
// Stop is a no-op — the engine outlives any single consumer.
//
void ldPernotLoopStart(LdPernotCache* cacheP, LdPernotQueryFunc queryFn)
{
  loopCache   = cacheP;
  loopQueryFn = queryFn;
  ldPeriodicLoopRegister(pernotTick, cacheP);
}



void ldPernotLoopStop(void)
{
  // Engine shutdown is owned by the broker app via ldPeriodicLoopStop.
}
