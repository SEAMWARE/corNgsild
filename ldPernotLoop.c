//
// FILE            ldPernotLoop.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <pthread.h>                                   // pthread_create, pthread_join
#include <stdbool.h>                                   // bool
#include <stdint.h>                                    // uint64_t
#include <string.h>                                    // strcmp, strlen, strcpy
#include <stdio.h>                                     // snprintf
#include <time.h>                                      // clock_gettime, nanosleep
#include <stdlib.h>                                    // malloc, free

#include "kalloc/KAlloc.h"                             // KAlloc, kaAlloc
#include "kalloc/kaBufferInit.h"                       // kaBufferInit
#include "kalloc/kaBufferReset.h"                      // kaBufferReset
#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjBuilder.h"                           // kjObject, kjString, kjArray, kjChildAdd
#include "kjson/kjRenderSize.h"                        // kjFastRenderSize
#include "kjson/kjRender.h"                            // kjFastRender
#include "kjson/kjClone.h"                             // kjClone
#include "kjson/kjLookup.h"                            // kjLookup

#include "swRest/SwRestState.h"                        // swRest (for thread-local init)
#include "swRest/swRestClient.h"                       // SwRestClientRequest, swRestClientSend

#include "swJsonld/swldCompactTree.h"                  // swldCompactTree
#include "swJsonld/swldInit.h"                         // swldCoreContext
#include "swJsonld/SwldContext.h"                      // SwldContext

#include "swNgsild/LdVocab.h"                          // LD_VOCAB_*
#include "swNgsild/LdPernotCache.h"                    // LdPernotCache, LdPernotItem
#include "swNgsild/ldEntityToApi.h"                    // ldEntityToApi
#include "swNgsild/ldStripSysAttrs.h"                  // ldStripSysAttrs
#include "swNgsild/ldPernotLoop.h"                     // Own interface



static volatile bool       loopRunning = false;
static pthread_t           loopThread;
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

  // Build notification tree
  KjNode* notification = kjObject(NULL, NULL);

  char notifId[80];
  snprintf(notifId, sizeof(notifId), "urn:ngsi-ld:Notification:%08x:%04x",
           (unsigned)(nowNanos() >> 16), (unsigned)(nowNanos() & 0xFFFF));

  char isoTimeBuf[64];
  isoFromNanos(nowNanos(), isoTimeBuf, sizeof(isoTimeBuf));

  kjChildAdd(notification, kjString(NULL, "id", notifId));
  kjChildAdd(notification, kjString(NULL, "type", "Notification"));
  kjChildAdd(notification, kjString(NULL, "subscriptionId", itemP->subId));
  kjChildAdd(notification, kjString(NULL, "notifiedAt", isoTimeBuf));

  // Convert entities from storage to API format
  KjNode* dataArray = kjArray(NULL, "data");
  for (KjNode* entityP = entityArray->value.firstChildP; entityP != NULL; entityP = entityP->next)
  {
    KjNode* entityClone = kjClone(NULL, entityP);
    ldEntityToApi(entityClone, kaP);
    ldStripSysAttrs(entityClone);
    kjChildAdd(dataArray, entityClone);
  }
  kjChildAdd(notification, dataArray);

  // Compact
  swldCompactTree(notification);

  // Render to JSON
  int bodySize = kjFastRenderSize(notification);
  char* body = (char*) kaAlloc(kaP, bodySize);
  kjFastRender(notification, body);

  // Send via HTTP
  SwRestClientRequest  req;
  SwRestClientResponse resp;

  swRestClientRequestInit(&req, SwVerbPost, itemP->endpointUri, kaP);
  swRestClientRequestHeader(&req, "Content-Type", "application/json");

  // Link header with @context
  SwldContext* ctxP = swldCoreContext();
  if (ctxP != NULL && ctxP->url != NULL)
  {
    char linkBuf[512];
    snprintf(linkBuf, sizeof(linkBuf),
             "<%s>; rel=\"http://www.w3.org/ns/json-ld#context\"; type=\"application/ld+json\"",
             itemP->contextUrl ? itemP->contextUrl : ctxP->url);
    swRestClientRequestHeader(&req, "Link", linkBuf);
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
      swRestClientRequestHeader(&req, kP->value.s, vP->value.s);
    }
  }

  swRestClientRequestBody(&req, body, strlen(body));
  // § 5.2.15 endpoint.timeout — per-sub override; default 10s
  int reqTmoMs = (itemP->timeoutMs > 0) ? itemP->timeoutMs : 10000;
  swRestClientRequestTimeout(&req, 5000, reqTmoMs);

  int rc = swRestClientSend(&req, &resp);

  if (rc == 0 && resp.statusCode >= 200 && resp.statusCode < 300)
    return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// pernotLoopThread -
//
static void* pernotLoopThread(void* vP)
{
  char           allocBuffer[8192];
  KAlloc         ka;
  struct timespec sleepTime = { 1, 0 };   // 1 second

  while (loopRunning)
  {
    uint64_t now = nowNanos();

    for (LdPernotItem* itemP = loopCache->head; itemP != NULL; itemP = itemP->next)
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
        uint64_t cool = (itemP->cooldownNs != 0) ? itemP->cooldownNs : 30000000000ULL;
        if (itemP->lastFailure + cool > now)
          continue;
        itemP->state = LdPernotActive;
      }

      uint64_t intervalNs = (uint64_t) itemP->timeInterval * 1000000000ULL;
      if (itemP->lastNotification + intervalNs > now)
        continue;

      // Due — query and send
      kaBufferInit(&ka, allocBuffer, sizeof(allocBuffer), 16384, NULL, "pernot-query");

      KjNode* entityArray = loopQueryFn(itemP->tenantP, itemP, &ka);

      itemP->lastNotification = now;
      itemP->timesSent++;

      if (entityArray == NULL || entityArray->value.firstChildP == NULL)
      {
        itemP->noMatch++;
        kaBufferReset(&ka, true);
        continue;
      }

      bool ok = pernotSendNotification(itemP, entityArray, &ka);

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

      kaBufferReset(&ka, true);
    }

    nanosleep(&sleepTime, NULL);
  }

  return NULL;
}



// ldPernotLoopStart
void ldPernotLoopStart(LdPernotCache* cacheP, LdPernotQueryFunc queryFn)
{
  loopCache   = cacheP;
  loopQueryFn = queryFn;
  loopRunning = true;

  pthread_create(&loopThread, NULL, pernotLoopThread, NULL);
}



// ldPernotLoopStop
void ldPernotLoopStop(void)
{
  loopRunning = false;
  pthread_join(loopThread, NULL);
}
