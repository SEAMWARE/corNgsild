//
// FILE            ldSubscriptionCounters.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Inject notification counters from the in-memory subscription cache
// into a subscription JSON tree for GET responses.
//
// Fields added to the "notification" object (per spec 5.2.14):
//   timesSent, timesFailed, lastNotification, lastSuccess, lastFailure
//
#include <stdio.h>                                     // snprintf
#include <time.h>                                      // gmtime_r, strftime

#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjLookup.h"                            // kjLookup
#include "kjson/kjBuilder.h"                           // kjInteger, kjString, kjChildAdd, kjChildRemove

#include "corRest/CorRestState.h"                        // corRest (kjsonP — the per-request render arena)
#include "corNgsild/LdVocab.h"                          // LD_VOCAB_NOTIFICATION
#include "corNgsild/LdSubCache.h"                       // LdSubCacheItem
#include "corNgsild/ldSubscriptionCounters.h"           // Own interface



// -----------------------------------------------------------------------------
//
// stripStoredStats - remove any prior stats fields in the notification
// object so our cache-authoritative inject can't produce duplicates. The
// fields may have been loaded by db.subscriptionRetrieve from a mongo doc
// that was flushed by us (or another broker) at some point.
//
static void stripStoredStats(KjNode* notifP)
{
  static const char* fields[] = {
    "timesSent", "timesFailed", "lastNotification", "lastSuccess", "lastFailure", "status"
  };

  for (int i = 0; i < 6; i++)
  {
    KjNode* existing;
    while ((existing = kjLookup(notifP, fields[i])) != NULL)
      kjChildRemove(notifP, existing);
  }
}



// -----------------------------------------------------------------------------
//
// nsToIso - convert epoch seconds (with fractional ms) to ISO 8601
//
static void nsToIso(uint64_t epochNs, char* buf, int bufSize)
{
  time_t      secs = (time_t) (epochNs / 1000000000ULL);
  long        ms   = (long) ((epochNs % 1000000000ULL) / 1000000);
  struct tm   tm;

  gmtime_r(&secs, &tm);
  int n = strftime(buf, bufSize, "%Y-%m-%dT%H:%M:%S", &tm);

  if (ms > 0)
    snprintf(buf + n, bufSize - n, ".%03ldZ", ms);
  else
  {
    buf[n++] = 'Z';
    buf[n]   = 0;
  }
}



// -----------------------------------------------------------------------------
//
// ldSubscriptionCountersInject -
//
void ldSubscriptionCountersInject(KjNode* subP, LdSubCacheItem* itemP)
{
  if (subP == NULL || itemP == NULL)
    return;

  // Find the notification object
  KjNode* notifP = kjLookup(subP, LD_VOCAB_NOTIFICATION);
  if (notifP == NULL)
    notifP = kjLookup(subP, "notification");
  if (notifP == NULL || notifP->type != KjObject)
    return;

  // Strip any persisted copies — our in-memory counters are authoritative
  // (flush writes them back on demand/timer). Unconditional so a zero-count
  // cache item doesn't leak a stale persisted value either.
  stripStoredStats(notifP);

  // Only add counters if at least one notification has been sent
  if (itemP->timesSent == 0)
    return;

  char isoBuf[64];

  kjChildAdd(notifP, kjInteger(corRest.kjsonP, "timesSent", itemP->timesSent));

  if (itemP->timesFailed > 0)
    kjChildAdd(notifP, kjInteger(corRest.kjsonP, "timesFailed", itemP->timesFailed));

  if (itemP->lastNotification > 0)
  {
    nsToIso(itemP->lastNotification, isoBuf, sizeof(isoBuf));
    kjChildAdd(notifP, kjString(corRest.kjsonP, "lastNotification", isoBuf));
  }

  if (itemP->lastSuccess > 0)
  {
    nsToIso(itemP->lastSuccess, isoBuf, sizeof(isoBuf));
    kjChildAdd(notifP, kjString(corRest.kjsonP, "lastSuccess", isoBuf));
  }

  if (itemP->lastFailure > 0)
  {
    nsToIso(itemP->lastFailure, isoBuf, sizeof(isoBuf));
    kjChildAdd(notifP, kjString(corRest.kjsonP, "lastFailure", isoBuf));
  }

  // notification.status (§ 5.2.14.2): "ok" if the most recent attempt
  // succeeded, "failed" if it failed. Derived from the timestamps —
  // whichever lastSuccess/lastFailure is more recent wins. If neither
  // ever happened (timesSent>0 is the gate that got us here), default
  // to "ok" — the increment without a success/failure stamp shouldn't
  // be possible, but stay defensive.
  const char* statusStr = "ok";
  if (itemP->lastFailure > itemP->lastSuccess)
    statusStr = "failed";
  kjChildAdd(notifP, kjString(corRest.kjsonP, "status", (char*) statusStr));
}



// -----------------------------------------------------------------------------
//
// ldPernotCountersInject - inject pernot subscription counters into a sub tree
//
void ldPernotCountersInject(KjNode* subP, LdPernotItem* itemP)
{
  if (subP == NULL || itemP == NULL)
    return;

  KjNode* notifP = kjLookup(subP, LD_VOCAB_NOTIFICATION);
  if (notifP == NULL)
    notifP = kjLookup(subP, "notification");
  if (notifP == NULL || notifP->type != KjObject)
    return;

  stripStoredStats(notifP);

  if (itemP->timesSent == 0)
    return;

  char isoBuf[64];

  kjChildAdd(notifP, kjInteger(corRest.kjsonP, "timesSent", itemP->timesSent));

  if (itemP->timesFailed > 0)
    kjChildAdd(notifP, kjInteger(corRest.kjsonP, "timesFailed", itemP->timesFailed));

  if (itemP->lastNotification > 0)
  {
    nsToIso(itemP->lastNotification, isoBuf, sizeof(isoBuf));
    kjChildAdd(notifP, kjString(corRest.kjsonP, "lastNotification", isoBuf));
  }

  if (itemP->lastSuccess > 0)
  {
    nsToIso(itemP->lastSuccess, isoBuf, sizeof(isoBuf));
    kjChildAdd(notifP, kjString(corRest.kjsonP, "lastSuccess", isoBuf));
  }

  if (itemP->lastFailure > 0)
  {
    nsToIso(itemP->lastFailure, isoBuf, sizeof(isoBuf));
    kjChildAdd(notifP, kjString(corRest.kjsonP, "lastFailure", isoBuf));
  }

  const char* statusStr = "ok";
  if (itemP->lastFailure > itemP->lastSuccess)
    statusStr = "failed";
  kjChildAdd(notifP, kjString(corRest.kjsonP, "status", (char*) statusStr));
}
