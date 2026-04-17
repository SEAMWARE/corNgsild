#ifndef SWNGSILD_LDSUBCACHE_H_
#define SWNGSILD_LDSUBCACHE_H_

//
// FILE            LdSubCache.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Per-tenant subscription cache with pre-parsed matching fields.
//
// The cache is a linked list of LdSubCacheItem, one per subscription.
// Each item holds the raw KjNode subscription tree (for rendering on GET)
// plus pre-parsed shortcuts for fast matching on entity writes.
//
// The raw KjNode tree is in @context-expanded form (as stored in DB).
// The pre-parsed fields avoid re-parsing on every entity write.
//
#include <regex.h>                                     // regex_t
#include <stdbool.h>                                   // bool
#include <stdint.h>                                    // uint64_t

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                              // KjNode

#include "swNgsild/LdQ.h"                              // LdQNode
#include "swNgsild/LdScopeExpr.h"                      // LdScopeExpr
#include "swNgsild/LdGeoRel.h"                         // LdGeoRel



// -----------------------------------------------------------------------------
//
// LdSubIdPattern - compiled regex for entity ID pattern matching
//
typedef struct LdSubIdPattern
{
  regex_t                regex;
  struct LdSubIdPattern* next;
} LdSubIdPattern;



// -----------------------------------------------------------------------------
//
// LdSubEntitySelector - pre-parsed EntitySelector from entities[] array
//
typedef struct LdSubEntitySelector
{
  char*                       type;         // expanded IRI (borrowed from subTree)
  char*                       id;           // exact entity id (borrowed, NULL if absent)
  LdSubIdPattern*             idPatternList;// compiled idPattern regexes (NULL if absent)
  struct LdSubEntitySelector* next;
} LdSubEntitySelector;



// -----------------------------------------------------------------------------
//
// LdSubCacheItem - single cached subscription
//
typedef struct LdSubCacheItem
{
  char*                     subId;          // subscription ID (malloc'd copy)
  KjNode*                   subTree;        // full subscription tree, expanded URIs (kjClone'd, malloc)

  // Pre-parsed matching shortcuts
  LdSubEntitySelector*      entitySelectors;// linked list of parsed entities[] items (NULL = match all)
  char**                    watchedAttrsV;  // NULL-terminated array of expanded attr IRIs (NULL = all)
  LdQNode*                  qExpr;          // parsed q-filter tree (NULL if no q)
  LdScopeExpr*              scopeExpr;      // parsed scopeQ (NULL if no scopeQ)
  LdGeoRel*                 geoRel;         // parsed geoQ.georel (NULL if no geoQ)
  char*                     geoGeometry;    // geoQ.geometry ("Point", "Polygon", etc.) (NULL if no geoQ)
  char*                     geoCoordinates; // geoQ.coordinates as JSON string (NULL if no geoQ)
  char*                     geoProperty;    // geoQ.geoproperty expanded IRI (default: location)
  char**                    notifAttrsV;    // NULL-terminated array of notification attribute IRIs (NULL = all)
  char**                    datasetIdV;    // NULL-terminated array of datasetId URIs + "@none" (NULL = all instances)
  int                       triggerMask;    // bitmask of LD_TRIGGER_* values (0 = use LD_TRIGGER_DEFAULT)

  // Subscription state (borrowed pointers into subTree)
  char*                     status;         // "active", "paused", "expired"
  char*                     endpointUri;    // notification.endpoint.uri
  char*                     contextUrl;     // jsonldContext URL for notification compaction
  char*                     format;         // notification format: NULL=normalized, "simplified", "concise"

  // Expiration
  uint64_t                  expiresAt;      // epoch nanoseconds (0 = no expiration)

  // Notification counters (mutable, updated on send)
  int                       timesSent;
  int                       timesFailed;
  uint64_t                  lastNotification; // epoch nanoseconds
  uint64_t                  lastSuccess;      // epoch nanoseconds
  uint64_t                  lastFailure;      // epoch nanoseconds
  double                    throttling;       // minimum seconds between notifications

  struct LdSubCacheItem*    next;           // linked list chain
} LdSubCacheItem;



// -----------------------------------------------------------------------------
//
// LdGeoMatchFunc - callback for geo matching (avoids GEOS dependency in swNgsild)
//
// Used for both subscription notification matching and queryEntities post-assembly.
//
typedef bool (*LdGeoMatchFunc)(KjNode* entityP, LdGeoRel* geoRel, const char* geometry,
                                const char* coordinates, const char* geoproperty);



// -----------------------------------------------------------------------------
//
// LdSubCache - per-tenant subscription cache
//
typedef struct LdSubCache
{
  LdSubCacheItem*     itemList;     // linked list head
  LdSubCacheItem*     last;         // linked list tail (O(1) append)
  LdGeoMatchFunc   geoMatchFunc; // registered by broker (NULL = skip geo check)
  KAlloc              alloc;        // persistent allocator for parsed trees (q, scope, etc.)
  char                allocBuf[1024]; // initial allocation buffer
} LdSubCache;

#endif  // SWNGSILD_LDSUBCACHE_H_
