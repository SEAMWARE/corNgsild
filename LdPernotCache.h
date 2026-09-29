#ifndef CORNGSILD_LDPERNOTCACHE_H_
#define CORNGSILD_LDPERNOTCACHE_H_

//
// FILE            LdPernotCache.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Separate cache for periodic notification (timeInterval) subscriptions.
// Kept apart from the regular LdSubCache so that entity-write matching
// (which walks LdSubCache on every write) never touches pernot subs.
// The background pernot loop thread walks only this cache.
//
#include <pthread.h>                                   // pthread_rwlock_t
#include <stdbool.h>                                   // bool
#include <stdint.h>                                    // uint64_t

#include "corTree/CorNode.h"                           // CorNode
#include "corAlloc/CorAlloc.h"                         // CorAlloc

#include "corNgsild/LdQ.h"                              // LdQNode
#include "corNgsild/LdScopeExpr.h"                      // LdScopeExpr
#include "corNgsild/LdGeoRel.h"                         // LdGeoRel
#include "corNgsild/LdSubCache.h"                       // LdSubEntitySelector (reuse)



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
  CorNode*               subTree;          // full subscription tree (corTreeClone'd, malloc)
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
  int                    timeoutMs;       // notification.endpoint.timeout  (§ 5.2.15) in ms; 0 = use 10s default
  CorNode*               receiverInfo;    // notification.endpoint.receiverInfo (§ 5.2.15) — Array of {key, value} from subTree, NULL if none
  char*                  notifJoin;       // notification.join (§ 5.2.14) — "flat" / "inline" / "@none" / NULL = absent
  int                    notifJoinLevel;  // notification.joinLevel (§ 5.2.14) — depth; 0 = absent (use spec default 1)

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

  // Tenant - the one the subscription was created in: queried (opaque, for db.entityQuery) and
  // named on its notifications (NGSILD-Tenant; "" = the default tenant)
  void*                  tenantP;
  char                   tenantName[64];

  // Pinning (as LdSubCacheItem): a reader that keeps the item past the cache lock - the loop
  // across its query and its send, a GET across its rendering - pins it. A remove of a pinned
  // item parks it on the cache's retiredList; only writers free, under the wrlock, at 0 pins.
  int                    refCount;
  bool                   retired;

  // The item's own arena, for what is parsed out of subTree (q, scopeQ, geoQ): freed WITH the
  // item. They were parsed into the cache's arena (or the subscription cache's!), which is never
  // freed - so every create/delete of a periodic subscription leaked its parsed filters.
  CorAlloc               alloc;
  char                   allocBuf[512];

  struct LdPernotItem*   next;
} LdPernotItem;



// -----------------------------------------------------------------------------
//
// LdPernotCache - global periodic-notification subscription cache
//
typedef struct LdPernotCache
{
  pthread_rwlock_t  lock;          // walks and lookups: rd - add / remove: wr (taken inside)
  LdPernotItem*     retiredList;   // removed while pinned, awaiting the last unpin
  LdPernotItem*  head;
  LdPernotItem*  tail;
  CorAlloc       alloc;
  char           allocBuf[1024];
} LdPernotCache;

#endif  // CORNGSILD_LDPERNOTCACHE_H_
