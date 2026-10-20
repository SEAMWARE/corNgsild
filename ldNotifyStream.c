//
// FILE            ldNotifyStream.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                     // bool
#include <stddef.h>                                      // NULL
#include <string.h>                                      // strcmp

#include "corNgsild/LdSubCache.h"                        // LdSubCache, LdSubCacheItem
#include "corNgsild/ldSubCache.h"                        // ldSubCacheRdLock, ldSubCacheItemLookup
#include "corNgsild/CorNgsild.h"                         // corNgsild
#include "corNgsild/ldNotifyStatsHook.h"                 // ldNotifyStatsHookInvoke
#include "corNgsild/ldNotifyStream.h"                    // Own interface

#if COR_WITH_EVENT_STREAM
// -----------------------------------------------------------------------------
//
// The application's hook - set once at startup, read by every notifying thread after that
//
static LdNotifyStreamPut streamPut = NULL;



// -----------------------------------------------------------------------------
//
// ldNotifyStreamSet -
//
void ldNotifyStreamSet(LdNotifyStreamPut putFn)
{
  streamPut = putFn;
}



// -----------------------------------------------------------------------------
//
// ldNotifyIsEventStream -
//
bool ldNotifyIsEventStream(const char* uri)
{
  return (uri != NULL) && (strcmp(uri, "urn:ngsi-ld:self") == 0);
}



// -----------------------------------------------------------------------------
//
// ldNotifyStreamOn -
//
bool ldNotifyStreamOn(void)
{
  return streamPut != NULL;
}



// -----------------------------------------------------------------------------
//
// ldNotifyStreamPut -
//
// Kept: nothing is counted yet - the stream counts it when it writes it (ldNotifyStreamSent). Not kept:
// a failure, with its cooldown, as for any binding.
//
void ldNotifyStreamPut(LdSubCacheItem* itemP, bool csrSub, const char* json, uint64_t now)
{
  if ((streamPut != NULL) && (streamPut(corNgsild.tenantP, itemP->subId, csrSub, json) == true))
    return;

  itemP->timesSent       += 1;
  itemP->timesFailed     += 1;
  itemP->lastNotification = now;
  itemP->lastFailure      = now;

  ldNotifyStatsHookInvoke(csrSub, false);
}



// -----------------------------------------------------------------------------
//
// ldNotifyStreamSent -
//
void ldNotifyStreamSent(LdSubCache* cacheP, const char* subId, bool csrSub, int n, uint64_t when)
{
  if ((cacheP == NULL) || (n <= 0))
    return;

  ldSubCacheRdLock(cacheP);

  LdSubCacheItem* itemP = ldSubCacheItemLookup(cacheP, subId);

  if (itemP != NULL)
  {
    itemP->timesSent       += n;
    itemP->lastNotification = when;
    itemP->lastSuccess      = when;
  }

  ldSubCacheUnlock(cacheP);

  for (int i = 0; i < n; i++)
    ldNotifyStatsHookInvoke(csrSub, true);
}
#endif
