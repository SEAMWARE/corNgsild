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

#include "swJsonld/SwldContext.h"                     // SwldContext
#include "swNgsild/LdFormat.h"                           // LdFormat
#include "swNgsild/LdGeoRel.h"                           // LdGeoRel
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

  char*   omit;
  char**  omitV;         // split + JSON-LD expanded

  char*   lang;          // language filter for LanguageProperty reduction

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

  // @context (set in parseHook)
  bool              contextError;
  SwldContext*       contextP;
  const char*        userContextUrl;  // URL from Link header or default context (NULL if none)

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
// ldParamHook - callback for swRest param validation
//
extern void ldParamHook(const char* name, const char* value);

#endif  // SWNGSILD_SWNGSILD_STATE_H_
