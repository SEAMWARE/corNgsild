#ifndef SWNGSILD_LDPERNOTCACHE_H_
#define SWNGSILD_LDPERNOTCACHE_H_

//
// FILE            LdPernotCache.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Separate cache for periodic notification (timeInterval) subscriptions.
// Kept apart from the regular LdSubCache so that entity-write matching
// (which walks LdSubCache on every write) never touches pernot subs.
// The background pernot loop thread walks only this cache.
//
#include <stdbool.h>                                   // bool
#include <stdint.h>                                    // uint64_t

#include "kjson/KjNode.h"                              // KjNode
#include "kalloc/KAlloc.h"                             // KAlloc

#include "swNgsild/LdQ.h"                              // LdQNode
#include "swNgsild/LdScopeExpr.h"                      // LdScopeExpr
#include "swNgsild/LdGeoRel.h"                         // LdGeoRel
#include "swNgsild/LdSubCache.h"                       // LdSubEntitySelector (reuse)



// -----------------------------------------------------------------------------
//
// LdPernotState - periodic subscription lifecycle state
//
typedef enum LdPernotState
{
  LdPernotActive = 0,
  LdPernotPaused,
  LdPernotExpired,
  LdPernotErroneous
} LdPernotState;



// -----------------------------------------------------------------------------
//
// LdPernotItem - one periodic-notification subscription
//
typedef struct LdPernotItem
{
  char*                  subId;            // subscription ID (malloc'd copy)
  KjNode*                subTree;          // full subscription tree (kjClone'd, malloc)
  int                    timeInterval;     // notification period in seconds

  // Pre-parsed query filters (built at cache-add time)
  LdSubEntitySelector*   entitySelectors;  // entity type/id/idPattern selectors
  char**                 notifAttrsV;      // notification attributes (NULL = all)
  char**                 datasetIdV;       // datasetId filter (NULL = all instances)
  LdQNode*               qExpr;           // q-filter tree (NULL if none)
  LdScopeExpr*           scopeExpr;       // scopeQ (NULL if none)
  LdGeoRel*              geoRel;          // geoQ.georel (NULL if none)
  char*                  geoGeometry;     // geoQ.geometry
  char*                  geoCoordinates;  // geoQ.coordinates
  char*                  geoProperty;     // geoQ.geoproperty

  // Notification config
  char*                  endpointUri;     // notification.endpoint.uri
  char*                  contextUrl;      // jsonldContext URL
  char*                  format;          // notification format (NULL=normalized)

  // State
  LdPernotState          state;
  uint64_t               expiresAt;       // epoch nanoseconds (0 = never)
  uint64_t               cooldownNs;      // notification.endpoint.cooldown (§ 5.2.15) in ns; 0 = use 30s default

  // Timestamps + counters (updated by the pernot loop thread)
  uint64_t               lastNotification; // epoch nanoseconds
  uint64_t               lastSuccess;
  uint64_t               lastFailure;
  int                    timesSent;
  int                    timesFailed;
  // Last values flushed to mongo — delta = current - lastFlushed, so a
  // concurrent $inc from another broker composes atomically.
  int                    lastFlushedSent;
  int                    lastFlushedFailed;
  int                    noMatch;         // queries that returned 0 entities
  int                    consecutiveErrors;

  // Tenant (opaque — set by the broker, used for db.entityQuery)
  void*                  tenantP;

  struct LdPernotItem*   next;
} LdPernotItem;



// -----------------------------------------------------------------------------
//
// LdPernotCache - global periodic-notification subscription cache
//
typedef struct LdPernotCache
{
  LdPernotItem*  head;
  LdPernotItem*  tail;
  KAlloc         alloc;
  char           allocBuf[1024];
} LdPernotCache;

#endif  // SWNGSILD_LDPERNOTCACHE_H_
