//
// FILE            ldSnapshotNotify.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// SnapshotNotification — see header.
//
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // uint64_t
#include <stdio.h>                                       // snprintf
#include <string.h>                                      // strlen
#include <time.h>                                        // gmtime_r

#include "ktrace/kTrace.h"                               // KT_E
#include "kalloc/kaAlloc.h"                              // kaAlloc
#include "kjson/KjNode.h"                                // KjNode
#include "kjson/kjBuilder.h"                             // kjObject, kjString, kjInteger, kjChildAdd
#include "kjson/kjLookup.h"                              // kjLookup
#include "kjson/kjClone.h"                               // kjClone
#include "kjson/kjRender.h"                              // kjFastRender

#include "corRest/CorRestState.h"                          // corRest
#include "corRest/corRestClient.h"                         // CorRestClientRequest, corRestClientSend, corRestClientRequestInit/Header/Body/Timeout
#include "corRest/CorRestVerb.h"                           // CorVerbPost

#include "corNgsild/LdSnapshotCache.h"                    // LdSnapshotCacheItem
#include "corNgsild/ldSnapshotNotify.h"                   // Own interface
#include "corNgsild/ldRequestSubstitute.h"                // ldRequestSubstitute (§ 6.3.18)


//
// nsToIso - format ns timestamp as ISO 8601 UTC.
//
static char* nsToIso(uint64_t ns)
{
  time_t  s   = (time_t) (ns / 1000000000ULL);
  long    ms  = (long)   ((ns % 1000000000ULL) / 1000000ULL);
  struct tm tm;
  gmtime_r(&s, &tm);

  char* buf = (char*) kaAlloc(&corRest.kalloc, 48);
  snprintf(buf, 48, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
           tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
  return buf;
}



//
// generateNotificationId - urn:ngsi-ld:Notification:<hex>:<hex>.
//
static char* generateNotificationId(void)
{
  static int counter = 0;
  char* buf = (char*) kaAlloc(&corRest.kalloc, 64);
  snprintf(buf, 64, "urn:ngsi-ld:Notification:%lx:%04x",
           (long) (corRest.requestStartTime / 1000000000ULL), ++counter & 0xFFFF);
  return buf;
}



void ldSnapshotNotify(LdSnapshotCacheItem* itemP, bool deleted)
{
  if (itemP == NULL || itemP->tree == NULL) return;

  KjNode* endpointP = kjLookup(itemP->tree, "endpoint");
  if (endpointP == NULL || endpointP->type != KjString || endpointP->value.s[0] == 0)
    return;

  // Build the SnapshotNotification body (§ 5.3.4).
  KjNode* notifP = kjObject(corRest.kjsonP, NULL);

  uint64_t now    = corRest.requestStartTime;
  uint64_t expNs  = deleted ? (now > 1000000000ULL ? now - 1000000000ULL : 0)  // 1s in the past
                            : itemP->expiresAt;

  kjChildAdd(notifP, kjString (corRest.kjsonP, "id",          generateNotificationId()));
  kjChildAdd(notifP, kjString (corRest.kjsonP, "type",        "SnapshotNotification"));
  kjChildAdd(notifP, kjString (corRest.kjsonP, "notifiedAt",  nsToIso(now)));
  kjChildAdd(notifP, kjString (corRest.kjsonP, "expiresAt",   nsToIso(expNs)));
  kjChildAdd(notifP, kjString (corRest.kjsonP, "snapshotId",  (char*) itemP->id));
  kjChildAdd(notifP, kjInteger(corRest.kjsonP, "snapshotPriority", itemP->priority));

  // snapshotStatus — when the notification signals deletion the spec
  // doesn't constrain the status field directly; the past expiresAt
  // is the deletion signal. Emit the in-cache status either way.
  KjNode* statusP = kjLookup(itemP->tree, "snapshotStatus");
  const char* statusStr = (statusP != NULL && statusP->type == KjString) ? statusP->value.s : "preparing";
  kjChildAdd(notifP, kjString(corRest.kjsonP, "snapshotStatus", (char*) statusStr));

  // snapshotQueriesDetails — copy whatever the cache holds.
  KjNode* detailsP = kjLookup(itemP->tree, "snapshotQueriesDetails");
  if (detailsP != NULL)
    kjChildAdd(notifP, kjClone(corRest.kjsonP, detailsP));

  // Render to JSON.
  char* body = (char*) kaAlloc(&corRest.kalloc, 8192);
  kjFastRender(notifP, body);

  // POST.
  CorRestClientRequest  req;
  CorRestClientResponse resp;

  corRestClientRequestInit(&req, CorVerbPost, endpointP->value.s, NULL);
  corRestClientRequestHeader(&req, "Content-Type", "application/json");

  // § 5.16.6 / § 5.2.15 receiverInfo → HTTP headers.
  KjNode* riP = kjLookup(itemP->tree, "receiverInfo");
  if (riP != NULL && riP->type == KjArray)
  {
    for (KjNode* kvP = riP->value.firstChildP; kvP != NULL; kvP = kvP->next)
    {
      if (kvP->type != KjObject) continue;
      KjNode* kP = kjLookup(kvP, "key");
      KjNode* vP = kjLookup(kvP, "value");
      if (kP != NULL && kP->type == KjString && vP != NULL && vP->type == KjString)
      {
        const char* hv = ldRequestSubstitute(kP->value.s, vP->value.s);
        if (hv != NULL)
          corRestClientRequestHeader(&req, kP->value.s, hv);
      }
    }
  }

  corRestClientRequestBody(&req, body, strlen(body));
  corRestClientRequestTimeout(&req, 5000, 10000);

  int rc = corRestClientSend(&req, &resp);
  if (rc != 0 || resp.statusCode < 200 || resp.statusCode >= 300)
    KT_E("snapshotNotify: POST %s failed (rc=%d, status=%d)",
         endpointP->value.s, rc, resp.statusCode);
}
