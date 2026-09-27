//
// FILE            ldSnapshotNotify.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
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
#include "corTree/CorNode.h"                             // CorNode
#include "corTree/corTreeBuilder.h"                      // corTreeObject, corTreeString, corTreeInteger, corTreeChildAdd
#include "corTree/corTreeLookup.h"                       // corTreeLookup
#include "corTree/corTreeClone.h"                        // corTreeClone
#include "corJson/corJsonRender.h"                       // corJsonFastRender

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

  CorNode* endpointP = corTreeLookup(itemP->tree, "endpoint");
  if (endpointP == NULL || endpointP->type != CorString || endpointP->value.s[0] == 0)
    return;

  // Build the SnapshotNotification body (§ 5.3.4).
  CorNode* notifP = corTreeObject(corRest.kallocP, NULL);

  uint64_t now    = corRest.requestStartTime;
  uint64_t expNs  = deleted ? (now > 1000000000ULL ? now - 1000000000ULL : 0)  // 1s in the past
                            : itemP->expiresAt;

  corTreeChildAdd(notifP, corTreeString (corRest.kallocP, "id", generateNotificationId()));
  corTreeChildAdd(notifP, corTreeString (corRest.kallocP, "type", "SnapshotNotification"));
  corTreeChildAdd(notifP, corTreeString (corRest.kallocP, "notifiedAt", nsToIso(now)));
  corTreeChildAdd(notifP, corTreeString (corRest.kallocP, "expiresAt", nsToIso(expNs)));
  corTreeChildAdd(notifP, corTreeString (corRest.kallocP, "snapshotId", (char*) itemP->id));
  corTreeChildAdd(notifP, corTreeInteger(corRest.kallocP, "snapshotPriority", itemP->priority));

  // snapshotStatus — when the notification signals deletion the spec
  // doesn't constrain the status field directly; the past expiresAt
  // is the deletion signal. Emit the in-cache status either way.
  CorNode* statusP = corTreeLookup(itemP->tree, "snapshotStatus");
  const char* statusStr = (statusP != NULL && statusP->type == CorString) ? statusP->value.s : "preparing";
  corTreeChildAdd(notifP, corTreeString(corRest.kallocP, "snapshotStatus", (char*) statusStr));

  // snapshotQueriesDetails — copy whatever the cache holds.
  CorNode* detailsP = corTreeLookup(itemP->tree, "snapshotQueriesDetails");
  if (detailsP != NULL)
    corTreeChildAdd(notifP, corTreeClone(corRest.kallocP, detailsP));

  // Render to JSON.
  char* body = (char*) kaAlloc(&corRest.kalloc, 8192);
  corJsonFastRender(notifP, body);

  // POST.
  CorRestClientRequest  req;
  CorRestClientResponse resp;

  corRestClientRequestInit(&req, CorVerbPost, endpointP->value.s, NULL);
  corRestClientRequestHeader(&req, "Content-Type", "application/json");

  // § 5.16.6 / § 5.2.15 receiverInfo → HTTP headers.
  CorNode* riP = corTreeLookup(itemP->tree, "receiverInfo");
  if (riP != NULL && riP->type == CorArray)
  {
    for (CorNode* kvP = riP->value.firstChildP; kvP != NULL; kvP = kvP->next)
    {
      if (kvP->type != CorObject) continue;
      CorNode* kP = corTreeLookup(kvP, "key");
      CorNode* vP = corTreeLookup(kvP, "value");
      if (kP != NULL && kP->type == CorString && vP != NULL && vP->type == CorString)
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
