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
#include <pthread.h>                                   // pthread_rwlock_t

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                              // KjNode

#include "swNgsild/LdFormat.h"                     // LdFormat
#include "swRest/SwRestIn.h"                  // SwMimeType
#include "swNgsild/LdSubStatus.h"                  // LdSubStatus
#include "swNgsild/LdQ.h"                              // LdQNode
#include "swNgsild/LdScopeExpr.h"                      // LdScopeExpr
#include "swNgsild/LdGeoRel.h"                         // LdGeoRel
#include "swNgsild/LdTypeExpr.h"                       // LdTypeExpr



// -----------------------------------------------------------------------------
//
// LdSubIdPattern - compiled regex for entity ID pattern matching
//
typedef struct LdSubIdPattern
{
  regex_t                regex;
  const char*            source;  // raw pattern string, borrowed from subTree
  struct LdSubIdPattern* next;
} LdSubIdPattern;



// -----------------------------------------------------------------------------
//
// LdSubEntitySelector - pre-parsed EntitySelector from entities[] array
//
typedef struct LdSubEntitySelector
{
  char*                       type;         // expanded IRI as text (borrowed from
                                            // subTree) — kept for diagnostics and
                                            // for the plain-string fast path
  LdTypeExpr*                 typeExpr;     // parsed § 4.17 type-selection
                                            // expression; non-NULL whenever
                                            // `type` is set. Use this for
                                            // matching — `type` is only the
                                            // raw text and won't honour
                                            // (A|B), A&B, !A operators.
  char*                       id;           // exact entity id (borrowed, NULL if absent)
  LdSubIdPattern*             idPatternList;// compiled idPattern regexes (NULL if absent)
  struct LdSubEntitySelector* next;
} LdSubEntitySelector;



// -----------------------------------------------------------------------------
//
// LdSubSubordinate - one derived subscription forwarded to a remote CSR
//
// When a local subscription's entity filter overlaps a registered Context
// Source (§ 5.8.1.4), the broker forwards a derived sub to that source.
// The mapping below lets the originating broker propagate later PATCH /
// DELETE to the remote copy and re-dispatch incoming notifications back
// to the parent's subscriber.
//
typedef struct LdSubSubordinate
{
  char*                    remoteSubId;     // sub-id created on the remote CSR (malloc'd)
  char*                    regId;           // CSR that owns this remote sub (malloc'd)
  int                      runNo;           // 1,2,3... — disambiguates multiple CSRs for the same parent
  struct LdSubSubordinate* next;
} LdSubSubordinate;



// -----------------------------------------------------------------------------
//
// LdThrottleEntry - one buffered (coalesced) entity in a throttled sub's
// dirty set. § 5.2.x throttling: a match landing INSIDE the window is buffered
// here (deduped by entity id) instead of dropped; the periodic flush re-queries
// the latest state and sends one coalesced notification when the window elapses.
// Storing the id (not a value clone) keeps the hot buffer path O(1) per update;
// the state is materialized once per window at flush. A DELETE can't be
// re-queried (the entity is gone), so its final state is captured here.
//
typedef struct LdThrottleEntry
{
  char*    entityId;     // expanded entity id (malloc'd copy)
  int      reasonsMask;  // OR of LD_TRIGGER_* across the buffered changes
  int      op;           // latest LdNotifyOp (delete wins over update)
  uint64_t deletedAtNs;  // epoch-ns when op is a delete; 0 otherwise
  KjNode*  deleteState;  // malloc clone of the entity at delete time (delete op only), else NULL
} LdThrottleEntry;



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
  LdQNode*                  csfExpr;        // parsed csf, matched against the CSR itself. § 5.2.12
                                            // applies it to a Context Source Registration
                                            // Subscription only — on an entity Subscription it is ignored
  LdScopeExpr*              scopeExpr;      // parsed scopeQ (NULL if no scopeQ)
  LdGeoRel*                 geoRel;         // parsed geoQ.georel (NULL if no geoQ)
  char*                     geoGeometry;    // geoQ.geometry ("Point", "Polygon", etc.) (NULL if no geoQ)
  char*                     geoCoordinates; // geoQ.coordinates as JSON string (NULL if no geoQ)
  char*                     geoProperty;    // geoQ.geoproperty expanded IRI (default: location)
  char**                    notifAttrsV;    // NULL-terminated array of notification attribute IRIs (NULL = all)
  char**                    notifPickV;     // NULL-terminated; notification.pick — entity-member projection (NULL = all)
  char**                    notifOmitV;     // NULL-terminated; notification.omit — entity-member exclusion (NULL = none)
  char*                     lang;           // BCP-47 language code; LanguageMap reduction on notification (NULL = no reduction)
  char**                    datasetIdV;    // NULL-terminated array of datasetId URIs + "@none" (NULL = all instances)
  int                       triggerMask;    // bitmask of LD_TRIGGER_* values (0 = use LD_TRIGGER_DEFAULT)

  // Subscription state (borrowed pointers into subTree)
  LdSubStatus               status;         // § 5.2.12 lifecycle (enum — wire form converted at parse/render)
  char*                     endpointUri;    // notification.endpoint.uri
  SwMimeType              endpointAccept; // notification.endpoint.accept (§ 5.2.15) — SwMimeJson default
  char*                     contextUrl;     // jsonldContext URL for notification compaction
  // ngsildConformance (§ 5.2.12 / § 4.3.6.8) — request notifications conformant
  // to an older NGSI-LD spec version. 0/0 = absent (no downgrade).
  short                     conformanceMajor;
  short                     conformanceMinor;
  LdFormat                  format;         // notification format (§ 5.2.14) — LdFormatNone = normalized default
  bool                      sysAttrs;       // notification.sysAttrs (default false)
  bool                      showChanges;    // notification.showChanges (default false) — implies previousValue/Object/LanguageMap

  // Expiration
  uint64_t                  expiresAt;      // epoch nanoseconds (0 = no expiration)

  // Notification counters (mutable, updated on send)
  int                       timesSent;
  int                       timesFailed;
  uint64_t                  lastNotification; // epoch nanoseconds
  uint64_t                  lastSuccess;      // epoch nanoseconds
  uint64_t                  lastFailure;      // epoch nanoseconds
  // Last values flushed to mongo — used to compute deltas so a concurrent
  // flush from another broker can $inc atomically without clobbering.
  int                       lastFlushedSent;
  int                       lastFlushedFailed;
  double                    throttling;       // minimum seconds between notifications
  int                       timeInterval;     // § 5.11.7 periodic CSR-Sub notification period in seconds (0 = change-driven only)
  uint64_t                  cooldownNs;       // notification.endpoint.cooldown (§ 5.2.15) in ns; 0 = use 30s default
  int                       timeoutMs;        // notification.endpoint.timeout  (§ 5.2.15) in ms; 0 = use 10s default
  KjNode*                   receiverInfo;     // notification.endpoint.receiverInfo (§ 5.2.15) — Array of {key, value} from subTree, NULL if none
  KjNode*                   notifierInfo;     // notification.endpoint.notifierInfo (§ 5.2.15) — used by transport-specific params (e.g. MQTT-QoS per § 7.2)
  char*                     notifJoin;        // notification.join (§ 5.2.14) — "flat" / "inline" / "@none" / NULL = absent
  bool                      notifJoinActive;  // precomputed: notifJoin set and != "@none"
  int                       notifJoinLevel;   // notification.joinLevel (§ 5.2.14) — depth; 0 = absent (use spec default 1)

  // § 5.2.x throttling — coalesce-to-latest dirty set. A match landing inside
  // the per-SUBSCRIPTION window is buffered here (deduped by id) instead of
  // dropped; the periodic flush (sole sender for a throttled sub) re-queries +
  // sends one coalesced notification when the window elapses, so a burst's
  // final state is never lost (the old drop tail-lost it). Guarded by dirtyLock:
  // the cache rwlock is only a RDLOCK during notify, so concurrent request
  // threads buffering into the same sub need their own mutex.
  LdThrottleEntry*          dirtyV;
  int                       dirtyN;
  int                       dirtyAlloc;
  pthread_mutex_t           dirtyLock;

  // Distributed-subscription mapping (§ 5.8.1.4) — list of derived subs
  // this local sub has on remote Context Sources. NULL when nothing
  // matched at create time.
  LdSubSubordinate*         subordinateP;
  int                       subordinateRunNo;  // monotonic counter for derived-sub naming

  // Concurrency refcount — see LdRegCacheItem for the model. A reader (the
  // post-response notify drain, the CSR-sub matchers) pins matched items under
  // the rdlock, then sends notifications LOCK-FREE (sends are slow) and unpins.
  // A writer deleting/replacing a pinned item parks it on retiredList; only
  // writers free, under the wrlock, once refCount → 0.
  int                       refCount;
  bool                      retired;

  struct LdSubCacheItem*    next;           // linked list chain (also chains
                                            // retiredList once unlinked)
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
  LdSubCacheItem*     retiredList;  // unlinked-but-still-pinned items awaiting reap
  LdGeoMatchFunc   geoMatchFunc; // registered by broker (NULL = skip geo check)
  KAlloc              alloc;        // persistent allocator for parsed trees (q, scope, etc.)
  char                allocBuf[1024]; // initial allocation buffer

  // Concurrency (mirror of LdRegCache): writers (sub CRUD service routines) take
  // the wrlock around the list mutation; readers (notify drain, CSR-sub match)
  // take the rdlock for the walk and pin items they use across a notification
  // send. LOCK ORDER: reg-cache lock BEFORE sub-cache lock — sub writers must
  // pin + release the sub-lock before touching the reg cache, never sub→reg.
  pthread_rwlock_t    lock;
} LdSubCache;

#endif  // SWNGSILD_LDSUBCACHE_H_
