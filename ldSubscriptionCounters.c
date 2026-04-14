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
#include "kjson/kjBuilder.h"                           // kjInteger, kjString, kjChildAdd

#include "swNgsild/LdVocab.h"                          // LD_VOCAB_NOTIFICATION
#include "swNgsild/LdSubCache.h"                       // LdSubCacheItem
#include "swNgsild/ldSubscriptionCounters.h"           // Own interface



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

  // Only add counters if at least one notification has been sent
  if (itemP->timesSent == 0)
    return;

  char isoBuf[64];

  kjChildAdd(notifP, kjInteger(NULL, "timesSent", itemP->timesSent));

  if (itemP->timesFailed > 0)
    kjChildAdd(notifP, kjInteger(NULL, "timesFailed", itemP->timesFailed));

  if (itemP->lastNotification > 0)
  {
    nsToIso(itemP->lastNotification, isoBuf, sizeof(isoBuf));
    kjChildAdd(notifP, kjString(NULL, "lastNotification", isoBuf));
  }

  if (itemP->lastSuccess > 0)
  {
    nsToIso(itemP->lastSuccess, isoBuf, sizeof(isoBuf));
    kjChildAdd(notifP, kjString(NULL, "lastSuccess", isoBuf));
  }

  if (itemP->lastFailure > 0)
  {
    nsToIso(itemP->lastFailure, isoBuf, sizeof(isoBuf));
    kjChildAdd(notifP, kjString(NULL, "lastFailure", isoBuf));
  }
}
