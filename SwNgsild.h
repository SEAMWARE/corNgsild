#ifndef SWNGSILD_SWNGSILD_STATE_H_
#define SWNGSILD_SWNGSILD_STATE_H_

//
// FILE            SwNgsild.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // uint64_t

#include "kjson/KjNode.h"                              // KjNode

#include "swRest/SwRestState.h"                        // swRest (per-conn userData binding)
#include "swJsonld/SwldContext.h"                     // SwldContext
#include "swNgsild/LdFormat.h"                           // LdFormat
#include "swNgsild/LdGeoRel.h"                           // LdGeoRel
#include "swNgsild/LdOrder.h"                            // LdOrderTerm
#include "swNgsild/LdQ.h"                               // LdQNode
#include "swNgsild/LdScopeExpr.h"                        // LdScopeExpr
#include "swNgsild/LdTypeExpr.h"                         // LdTypeExpr
#include "swNgsild/ldCsrSubNotify.h"                     // CsrSubPending (per-conn deferred cache)
#include "swNgsild/ldSubscriptionNotify.h"              // LdNotifyPendingEntry (per-conn deferred cache)
#include "swNgsild/LdSubCache.h"                         // LdSubCache (notify pending cache pointer)
#include "swNgsild/ldRegCache.h"                         // ProbePending, PROBE_PENDING_MAX (per-conn)




// -----------------------------------------------------------------------------
//
// LdExpiredEntity - a transient Entity a read found past its expiresAt
//
// § 5.2.4. Queued during the read, deleted in the post-response hook. Lives in
// the per-connection state, NOT a thread-local: the hook does not necessarily
// run on the thread that served the request.
//
#define LD_EXPIRED_PENDING_MAX 64

typedef struct LdExpiredEntity
{
  void*       tenantP;    // Tenant* — opaque here, the broker owns the type
  const char* entityId;   // request-arena copy
} LdExpiredEntity;


// -----------------------------------------------------------------------------
//
// SwNgsild - per-request NGSI-LD state (thread-local)
//
typedef struct SwNgsild
{
  // URL parameters — string values + derived arrays
  char*   id;
  char**  idV;           // split from id on commas
  char*   idPattern;     // regex pattern for entity ID matching

  char*        type;
  char**       typeV;         // split + JSON-LD expanded (simple OR only)
  LdTypeExpr*  typeExpr;      // parsed type selection expression (OR-of-AND)

  char*   datasetId;
  char**  datasetIdV;    // split from datasetId on commas (URIs + "@none")

  char*         scopeQ;
  LdScopeExpr*  scopeExpr;     // parsed scopeQ expression (OR-of-AND)

  char*     q;
  LdQNode*  qExpr;             // parsed q expression tree (or NULL)

  // § 5.2.23 csf — the Context Source Filter. q-shaped, but matched against the
  // Context Source REGISTRATIONS, never against the queried Entities.
  char*     csf;
  LdQNode*  csfExpr;           // parsed csf expression tree (or NULL)

  char*               pick;
  char**              pickV;     // top-level names, split + JSON-LD expanded
  struct LdProjItem*  pickTree;  // § 4.21 NGSI-LD Attribute Projection Language —
                                 // full tree (with linked-entity nesting), expanded.

  // § 5.10.2 CSR Discovery: `attrs` is a deprecated synonym for pick+q.
  // Both accepted on that route, never together (handler returns 400).
  char*   attrs;
  char**  attrsV;        // split + JSON-LD expanded

  char*               omit;
  char**              omitV;     // top-level names, split + JSON-LD expanded
  struct LdProjItem*  omitTree;  // full tree, expanded.

  char*   lang;          // language filter for LanguageProperty reduction

  // PATCH /entities/{id} (§ 6.5.3.4): default observedAt to inject into each
  // merged attribute instance whose target already has an observedAt sub.
  // observedAtNs is 0 when the URL param is not set.
  char*      observedAt;
  uint64_t   observedAtNs;

  char*   expandValues;
  char**  expandValuesV; // split + JSON-LD expanded — attribute names whose values to expand in q

  char*   jsonKeys;
  char**  jsonKeysV;     // split + JSON-LD expanded — attribute names whose values are opaque (no expansion)

  char*   geometryProperty;  // GeoProperty for GeoJSON geometry field (short name from URL param)
  char*   geometryPropertyExpanded;  // expanded IRI of the geom property (default "location"), set for geo+json — protected from pick/omit/attrs so the geometry survives projection (§ 5.3.3.2)
  bool    geoJsonGeomForced;         // true when the geom property is kept ONLY for the geometry field (user projection would have dropped it) → ldToGeoJson removes it from "properties"

  // URL parameters — geo-query
  char*      georel;       // raw georel string (e.g. "near;maxDistance==1000")
  LdGeoRel*  geoRel;       // parsed georel
  char*      geometry;     // reference geometry type (e.g. "Point", "Polygon")
  char*      coordinates;  // reference geometry coordinates (JSON array string)
  char*      geoproperty;  // geoproperty to query (expanded IRI, or NULL for default "location")

  // URL parameters — temporal query (§ 4.11)
  char*      timerel;      // "before" / "after" / "between" — NULL if no temporal query
  char*      timeAt;       // raw ISO 8601 DateTime string (mandatory when timerel set)
  uint64_t   timeAtNs;     // timeAt parsed to epoch nanoseconds (0 if unset)
  char*      endTimeAt;    // raw ISO 8601 DateTime string (only for timerel=between)
  uint64_t   endTimeAtNs;  // endTimeAt parsed to epoch nanoseconds (0 if unset)
  char*      timeproperty; // observedAt / createdAt / modifiedAt / deletedAt — NULL = default observedAt
  int        lastN;        // ?lastN= per-attribute instance limit, descending order (0 = unset; § 6.4.7.3)
  int        firstN;       // ?firstN= per-attribute instance limit, ascending order (0 = unset; § 6.4.7.3)
  int        offsetN;      // ?offsetN= temporal pagination offset (with firstN or lastN; § 6.4.7.3)

  // URL parameters — aggregated temporal representation (§ 4.5.20)
  char*      aggrMethods;        // raw CSV: e.g. "sum,avg,min"
  char**     aggrMethodsV;       // split (NULL-terminated)
  char*      aggrPeriodDuration; // ISO 8601 duration: PT1H, PT5M, P1D, ... (NULL = whole window)

  // URL parameters — pagination
  int      limit;        // parsed limit value (default 20, set in preDispatchHook)
  int      offset;       // parsed offset value (default 0)

  // URL parameters — representation format
  LdFormat format;       // normalized/concise/simplified (from ?format= or ?options=keyValues)

  //
  // The Attribute a storage layer refused because the tenant already holds that
  // name as the other kind (GeoProperty vs anything else). Set with
  // DB_GEO_TYPE_CONFLICT, read by the service routine building the 409 so it can
  // name the attribute instead of describing the symptom. Points into the request
  // tree, so it lives exactly as long as the request does. Per-request state, and
  // therefore in here rather than a static of its own.
  //
  const char* geoConflictAttr;

  // URL parameters — booleans
  bool    sysAttrs;
  bool    dateTimeRounded;   // a DateTime input lost precision past nanoseconds — NGSILD-Warning 214 already sent
  bool    count;         // true if ?count=true
  bool    local;         // true if ?local=true
  bool    noForward;     // true if ?noForward=true  (discovery: local + CSR metadata, no forward)

  // Set by ldDistOpLoopReap when a distributed write loop-blocked an
  // exclusive/redirect forward (§ 6.3.18): the data is held externally and is
  // now unreachable. ldRenderHook rewrites a terminal 404 ResourceNotFound into
  // 508 Loop Detected when this is set (the entity was "not found" only because
  // its sole holder resolves back to us through the loop).
  bool    loopBlocked508;
  int     hops;          // ?hops=<N> — federation hop limit (default 8 when absent)
  bool    hopsSet;       // true if ?hops=... was explicitly supplied
  bool    details;       // true if ?details=true
  bool    noOverwrite;   // true if ?options=noOverwrite (Append Attributes)
  bool    deleteAll;     // true if ?deleteAll=true (Delete Attribute § 5.6.5)
  bool    upsertUpdate;  // true if ?options=update on Batch Upsert (default is "replace")

  // Purge Entities (§ 6.4.3.3)
  char**  dropV;         // ?drop=a,b — restrictive attr list (delete only these attrs)
  char**  keepV;         // ?keep=a,b — exclusionary attr list (delete everything except these)

  // URL parameters — ordering (§ 4.23)
  char*         orderBy;         // raw orderBy string
  LdOrderTerm*  orderByV;        // parsed terms (NULL-terminated array, expanded in ldExpandParams)
  int           orderByCount;    // number of terms
  char*         collation;       // BCP47 collation tag (NULL = default)
  char*         orderFrom;       // ?orderFrom= reference coordinates (JSON array) for dist-* sort (§ 7.6.2.2)
  char*         orderGeometry;   // ?orderGeometry= reference geometry type (default Point)

  // URL parameters — entity map + split entities
  bool    entityMapCreate;    // true if ?entityMap=true (create new map)
  char*   entityMapId;        // non-NULL if ?entityMap=<mapId> (page from existing)
  bool    splitEntitiesSet;   // true if ?splitEntities= was present in URL
  bool    splitEntitiesVal;   // value of ?splitEntities= (only valid if splitEntitiesSet)

  // URL parameters — linked entity retrieval (§ 4.5.23)
  char*   join;             // "flat" | "inline" | "@none" (default), NULL = absent
  int     joinLevel;        // depth limit for relationship walk; 0 = absent or @none
  char**  containedByV;     // § 5.7.1.4 — entity ids already encountered (cycle prevention); NULL-terminated, NULL = absent
  int     containedByCount; // count for convenience

  // URL parameters — strings
  char*   kind;          // ?kind= for GET /jsonldContexts

  // @context (set in parseHook)
  bool              contextError;
  SwldContext*       contextP;
  const char*        userContextUrl;  // URL from Link header or default context (NULL if none)
  KjNode*            userContextBody; // Pointer to the in-body @context node, captured BEFORE
                                      // swldExpandTree strips it from the tree. Set when the
                                      // user supplied an @context array or inline-object body
                                      // form; NULL for a single-URL or absent @context. Used
                                      // by postSubscriptions to auto-populate jsonldContext —
                                      // an array/object @context becomes an ImplicitlyCreated
                                      // @context entry whose served URL is the field's value.

  // Per-element errors raised at parse time on batch ops — array of
  // BatchEntityError objects. Set in ldParseHook for cases the framework
  // catches before the service routine sees the body (e.g. ld+json array
  // element missing @context). The batch handler drains this list into
  // its own errors[] before normal processing.
  KjNode*  batchPreErrors;

  // § 4.17 — parse-once cache for subscription entities[].type
  // expressions. Set by ldCheckSubscription (one slot per entities[]
  // entry, indexed left-to-right; NULL slots mean "no type field" or
  // "plain literal — no expression to keep"). Drained by
  // ldSubCacheItemAdd which transfers ownership to the cache. Trees
  // are malloc-allocated (KAlloc would die with the request);
  // entitySelectorsFree calls ldTypeExprFree when not consumed.
  LdTypeExpr** subEntityTypeExprsV;
  int          subEntityTypeExprsN;

  // Response flags
  bool    rawResponse;  // true => renderHook skips ldEntityToApi (used for subscription responses)
  bool    entityMapOnly; // true => GET|POST /entityMaps: query + return the EntityMap (not entities)

  // Tenant (resolved in preServiceHook, opaque to swNgsild)
  void*  tenantP;

  // Inc6c — per-connection deferred caches (were static __thread in their .c
  // files). Drained by the post-response hook; the realloc'd buffers are freed
  // in swNgsildStateFree when the connection's state is released.
  CsrSubPending*         csrPendingV;        // ldCsrSubNotify.c
  int                    csrPendingN;
  int                    csrPendingCap;

  LdNotifyPendingEntry*  pendingV;           // ldNotifyDefer.c
  int                    pendingN;
  int                    pendingCap;
  LdSubCache*            pendingCache;

  ProbePending           probePendingV[PROBE_PENDING_MAX];  // ldRegCache.c (fixed)
  int                    probePendingN;

  LdExpiredEntity        expiredV[LD_EXPIRED_PENDING_MAX];   // dbExpiredEntities.c (fixed)
  int                    expiredN;

  // TRoE deferred-event queue (troeDispatch.c). Opaque TroeEvent* (a broker
  // type) — the events live in the request arena, so nothing here is freed.
  // Per-connection so the worker that runs the request and the I/O thread that
  // drains it post-response see the same queue.
  void*                  troeQHead;
  void*                  troeQTail;
  int                    troeQCount;

} SwNgsild;



// -----------------------------------------------------------------------------
//
// swNgsild - per-request NGSI-LD state, reached through the per-connection
// swRest.userData slot (allocated/freed by the swRest userData hooks, see
// swNgsildStateCreate/Free). Background threads — and any path before a
// connection's userData is bound — fall back to a per-thread object, so every
// access stays crash-proof. Holding this per-CONNECTION (not __thread) is what
// makes processing a request off the I/O thread safe: the worker binds the
// connection's swRest (and thus its swNgsild) regardless of which thread runs.
//
extern __thread SwNgsild swNgsildFallback;

static inline SwNgsild* swNgsildBind(void)
{
  SwNgsild* p = (SwNgsild*) swRest.userData;
  return (p != NULL) ? p : &swNgsildFallback;
}

#define swNgsild (*swNgsildBind())



// ldNotifyValueChangeOnly - --notifyValueChangeOnly CLI flag: suppress a
// notification for an update that leaves the attribute value unchanged (default
// false = NGSI-LD compliant, always notify).
extern bool ldNotifyValueChangeOnly;





// -----------------------------------------------------------------------------
//
// ldDefaultContextUrl - default user @context URL (set by --userContext CLI arg, or NULL)
//
extern char* ldDefaultContextUrl;
extern uint64_t ldDefaultCooldownNs;   // --cooldownMillis: default endpoint cooldown after a delivery failure (0 = only when the subscription specifies one)



// -----------------------------------------------------------------------------
//
// ldSplitEntities - global flag, default TRUE (standard NGSI-LD behavior).
// Set to false via --noSplitEntities CLI arg.
// When true, distributed queries collect ALL entity IDs from all sources,
// assemble complete entities, then apply filters post-assembly.
// When false (no-split), each entity is fully at one source — forward
// the full query, merge + dedup.
//
extern bool ldSplitEntities;



// -----------------------------------------------------------------------------
//
// ldDistributed - global flag, default FALSE. Set via --distributed / -dist.
//
// Distributed operations cost a registration-cache lookup on EVERY entity
// operation, and most deployments hold all their data locally and never
// register a Context Source. So the whole mechanism is opt-in, exactly like
// TRoE: with it off, the forwarding matchers report no matches and every
// operation stays local.
//
// It gates FORWARDING only. The Registry API itself (create/update/delete/
// discover Context Source Registrations, § 12) keeps working either way —
// ldRegCacheMatchForDiscovery is deliberately NOT gated — so registrations can
// be provisioned before the feature is switched on.
//
extern bool ldDistributed;

// System-attribute timestamps render at most 6 fractional digits (§ 5.2.2.4) by default;
// --high-precision/-hp opts into the full 9 (nanoseconds). See ldUrlParams.c.
extern bool ldTimestampHighPrecision;



// ldCsourceAliasBase - per-broker contextSourceAlias base (NGSI-LD § 5.7.5 /
// RFC 7230 pseudonym), used in Via headers for distributed-op loop detection.
// Set by --csourceAlias CLI arg, defaults to "<exe-basename>:<port>".
//
// The per-tenant alias used in Via headers is derived from this base; see
// ldCsourceAliasForTenant.
//
extern const char* ldCsourceAliasBase;



// ldBrokerStartTimeSec - epoch seconds at which the broker process started.
// Used by /info/sourceIdentity to compute contextSourceUptime. Set once at
// startup.
//
extern long long ldBrokerStartTimeSec;



// ldBrokerHttpEndpoint - this broker's externally-reachable HTTP base URL,
// e.g. "http://localhost:1026". Used to build the notification.endpoint.uri
// of derived subscriptions forwarded to remote Context Sources (§ 5.8.1.4)
// so their notifications loop back to this broker. Set once at startup
// from --httpEndpoint (CLI) or auto-derived from --port. NULL disables
// distributed-subscription fanout.
//
extern const char* ldBrokerHttpEndpoint;



// ldContextSourceExtras - optional opaque JSON config (§ 5.2.40) rendered
// verbatim under /info/sourceIdentity → "contextSourceExtras". The spec
// is explicit that this content shall NOT be expanded as JSON-LD, so the
// tree is parsed once at startup and cloned into the response arena on
// each request without going through @context. NULL when not configured.
//
extern KjNode* ldContextSourceExtras;



// -----------------------------------------------------------------------------
//
// ldParamHook - callback for swRest param validation
//
extern void ldParamHook(const char* name, const char* value);



// -----------------------------------------------------------------------------
//
// ldContextResolve - resolve @context from Link header or fall back to core context
//
extern void ldContextResolve(void);

#endif  // SWNGSILD_SWNGSILD_STATE_H_
