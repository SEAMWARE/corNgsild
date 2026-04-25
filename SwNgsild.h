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

#include "swJsonld/SwldContext.h"                     // SwldContext
#include "swNgsild/LdFormat.h"                           // LdFormat
#include "swNgsild/LdGeoRel.h"                           // LdGeoRel
#include "swNgsild/LdOrder.h"                            // LdOrderTerm
#include "swNgsild/LdQ.h"                               // LdQNode
#include "swNgsild/LdScopeExpr.h"                        // LdScopeExpr
#include "swNgsild/LdTypeExpr.h"                         // LdTypeExpr



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

  char*   pick;
  char**  pickV;         // split + JSON-LD expanded

  // § 5.10.2 CSR Discovery: `attrs` is a deprecated synonym for pick+q.
  // Both accepted on that route, never together (handler returns 400).
  char*   attrs;
  char**  attrsV;        // split + JSON-LD expanded

  char*   omit;
  char**  omitV;         // split + JSON-LD expanded

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

  // URL parameters — geo-query
  char*      georel;       // raw georel string (e.g. "near;maxDistance==1000")
  LdGeoRel*  geoRel;       // parsed georel
  char*      geometry;     // reference geometry type (e.g. "Point", "Polygon")
  char*      coordinates;  // reference geometry coordinates (JSON array string)
  char*      geoproperty;  // geoproperty to query (expanded IRI, or NULL for default "location")

  // URL parameters — pagination
  int      limit;        // parsed limit value (default 20, set in requestStartHook)
  int      offset;       // parsed offset value (default 0)

  // URL parameters — representation format
  LdFormat format;       // normalized/concise/simplified (from ?format= or ?options=keyValues)

  // URL parameters — booleans
  bool    sysAttrs;
  bool    count;         // true if ?count=true
  bool    local;         // true if ?local=true
  bool    noForward;     // true if ?noForward=true  (discovery: local + CSR metadata, no forward)
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
  char*         orderBy;       // raw orderBy string
  LdOrderTerm*  orderByV;      // parsed terms (NULL-terminated array, expanded in ldExpandParams)
  int           orderByCount;  // number of terms
  char*         collation;     // BCP47 collation tag (NULL = default)

  // URL parameters — entity map + split entities
  bool    entityMapCreate;    // true if ?entityMap=true (create new map)
  char*   entityMapId;        // non-NULL if ?entityMap=<mapId> (page from existing)
  bool    splitEntitiesSet;   // true if ?splitEntities= was present in URL
  bool    splitEntitiesVal;   // value of ?splitEntities= (only valid if splitEntitiesSet)

  // URL parameters — strings
  char*   kind;          // ?kind= for GET /jsonldContexts

  // @context (set in parseHook)
  bool              contextError;
  SwldContext*       contextP;
  const char*        userContextUrl;  // URL from Link header or default context (NULL if none)

  // Response flags
  bool    rawResponse;  // true => renderHook skips ldEntityToApi (used for subscription responses)
  bool    entityMapOnly; // true => GET|POST /entityMaps: query + return the EntityMap (not entities)

  // Tenant (resolved in preServiceHook, opaque to swNgsild)
  void*  tenantP;

} SwNgsild;



// -----------------------------------------------------------------------------
//
// swNgsild - thread-local per-request state
//
extern __thread SwNgsild swNgsild;



// -----------------------------------------------------------------------------
//
// ldLocalOnly - global flag set by --localOnly CLI arg
//
extern bool ldLocalOnly;



// -----------------------------------------------------------------------------
//
// ldDefaultContextUrl - default user @context URL (set by --userContext CLI arg, or NULL)
//
extern char* ldDefaultContextUrl;



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
