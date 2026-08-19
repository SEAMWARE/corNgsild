//
// FILE            ldParamsValidate.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                     // bool
#include <stdio.h>                                       // snprintf
#include <string.h>                                      // strcmp, strchr

#include "corRest/corRest.h"                               // corRest
#include "corRest/CorRestService.h"                        // CorRestService.ldOp
#include "corNgsild/CorNgsild.h"                           // corNgsild
#include "corNgsild/LdOp.h"                               // LdOpQueryEntities, LdOpQueryTemporal, LdOpBatchQuery
#include "corNgsild/ldParams.h"                           // LD_PARAM_GEOREL, LD_PARAM_GEOMETRY, ...
#include "corNgsild/LdProblem.h"                          // LD_ERROR_*
#include "corNgsild/ldError.h"                            // ldError
#include "corNgsild/ldCheckGeo.h"                         // ldCheckGeoQuery
#include "corRest/CorRestIn.h"                      // corAcceptParse, CorMimeGeoJson

#include "corNgsild/ldParamsValidate.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// looksLikeUri -
//
// Cheap shape check shared with the path-segment validator (corRest):
// must contain a non-leading colon followed by something, no whitespace.
//
static bool looksLikeUri(const char* s)
{
  if (s == NULL || s[0] == 0)
    return false;
  const char* colon = strchr(s, ':');
  if (colon == NULL || colon == s || colon[1] == 0)
    return false;
  for (const char* p = s; *p != 0; p++)
    if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
      return false;
  return true;
}



static bool nameInArray(const char* name, char** arr)
{
  if (arr == NULL || name == NULL) return false;
  for (int i = 0; arr[i] != NULL; i++)
    if (strcmp(name, arr[i]) == 0)
      return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// ldParamsValidate - validate cross-parameter constraints on URL params
//
bool ldParamsValidate(void)
{
  // limit=0 is only valid when count=true (NGSI-LD spec clause 6.3.10)
  if (corNgsild.limit == 0 && corNgsild.count == false)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request", "limit=0 is only valid when count=true");
    return true;
  }

  // § 6.4.7.3: firstN paginates ascending, lastN descending — both at
  // once is contradictory.
  if (corNgsild.firstN > 0 && corNgsild.lastN > 0)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
            "?firstN= and ?lastN= cannot be combined (ascending vs descending temporal pagination)");
    return true;
  }

  // § 4.21: pick and omit are alternative projections — listing the same
  // entity member in both is contradictory and shall be 400.
  if (corNgsild.pickV != NULL && corNgsild.omitV != NULL)
  {
    for (int i = 0; corNgsild.pickV[i] != NULL; i++)
    {
      if (nameInArray(corNgsild.pickV[i], corNgsild.omitV))
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
                "'%s' appears in both pick and omit", corNgsild.pickV[i]);
        return true;
      }
    }
  }

  // § 6.3.20 / § 5.10.2: `attrs` is a deprecated synonym for pick. Mixing
  // it with either pick or omit is contradictory.
  if (corNgsild.attrs != NULL && (corNgsild.pick != NULL || corNgsild.omit != NULL))
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
            "?attrs= cannot be combined with ?pick= or ?omit=");
    return true;
  }

  // ?id= URL param: each entry shall be a valid URI (§ 5.7.2.6 wording
  // applied to the array form).
  if (corNgsild.idV != NULL)
  {
    for (int i = 0; corNgsild.idV[i] != NULL; i++)
    {
      if (!looksLikeUri(corNgsild.idV[i]))
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
                "'?id': '%s' is not a valid URI", corNgsild.idV[i]);
        return true;
      }
    }
  }

  // § 6.4.3 attrs: list of attribute names. The reserved entity members
  // id/type/scope/@context are not attribute names — passing them in
  // ?attrs= is BadRequestData. (?pick allows them; that's a different
  // language by design.)
  if (corNgsild.attrsV != NULL)
  {
    for (int i = 0; corNgsild.attrsV[i] != NULL; i++)
    {
      const char* a = corNgsild.attrsV[i];
      if (strcmp(a, "id")       == 0 ||
          strcmp(a, "type")     == 0 ||
          strcmp(a, "scope")    == 0 ||
          strcmp(a, "@context") == 0)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
                "'?attrs': '%s' is a reserved entity member, not an Attribute name", a);
        return true;
      }
    }
  }

  // § 7.2.4 — a geo-query is the georel + geometry + coordinates triple
  // (plus optional geoproperty). Any of the three without the others is an
  // incomplete geo-query → 400, on every query route (ETSI 037_03_03 sends
  // a bare georel to /csourceRegistrations). Hoisted from getEntities so
  // the messages stay identical across routes.
  //
  // Skipped when the URL-param parser already raised an error: a rejected
  // value (georel= empty, geometry=Piont, …) sets a 400, and the generic
  // "incomplete" message must not clobber the specific one. Presence is the
  // received-params bitmask (was it given?), not the parsed pointer — so a
  // given-but-rejected param still counts as present (and the 400 gate above
  // keeps its specific message).
  if (corRest.out.httpStatusCode < 400)
  {
    bool hasGeorel      = (corRest.in.uriParamMask & LD_PARAM_GEOREL)      != 0;
    bool hasGeometry    = (corRest.in.uriParamMask & LD_PARAM_GEOMETRY)    != 0;
    bool hasCoordinates = (corRest.in.uriParamMask & LD_PARAM_COORDINATES) != 0;
    bool hasGeoproperty = (corRest.in.uriParamMask & LD_PARAM_GEOPROPERTY) != 0;

    if (hasGeorel || hasGeometry || hasCoordinates)
    {
      if (!hasGeorel || !hasGeometry || !hasCoordinates)
      {
        char missing[128] = "";
        int  len          = 0;

        if (!hasGeorel)      len += snprintf(missing + len, sizeof(missing) - len, "georel");
        if (!hasGeometry)    len += snprintf(missing + len, sizeof(missing) - len, "%sgeometry",    len > 0 ? ", " : "");
        if (!hasCoordinates) len += snprintf(missing + len, sizeof(missing) - len, "%scoordinates", len > 0 ? ", " : "");

        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query", "incomplete geo-query: missing %s", missing);
        return true;
      }
    }
    else if (hasGeoproperty)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query", "geoproperty without georel, geometry, and coordinates");
      return true;
    }
  }

  // § 4.10 / § 5.7.2.4 — when a geo-query is supplied (georel + geometry +
  // coordinates) the geometry shape must be a valid GeoJSON. We parse the
  // coordinates string and run the same shape/range/self-intersection
  // checks we apply to entity GeoProperty values, so an invalid query
  // polygon surfaces as 400 BadRequestData here instead of as a 500 from
  // the DB layer (mongo's 2dsphere is strict about edge crossings).
  if (corNgsild.geometry != NULL && corNgsild.coordinates != NULL)
  {
    if (!ldCheckGeoQuery(corNgsild.geometry, corNgsild.coordinates))
      return true;
  }

  // 'within' maps to MongoDB $geoWithin, which requires an areal reference
  // geometry (Polygon / MultiPolygon). A Point or LineString reference is
  // meaningless for "within" and must be rejected here as 400 — otherwise it
  // surfaces as a 500 leaking the raw $geoWithin error from the DB layer.
  // Gated on no prior error so a bad geometry/coordinates value (rejected
  // earlier in ldUrlParams) keeps its own, more specific message.
  if (corRest.out.httpStatusCode < 400 &&
      corNgsild.geoRel != NULL && corNgsild.geoRel->rel == LdGeoWithin && corNgsild.geometry != NULL)
  {
    if (strcmp(corNgsild.geometry, "Polygon") != 0 && strcmp(corNgsild.geometry, "MultiPolygon") != 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query",
              "georel 'within' requires a Polygon or MultiPolygon reference geometry (got '%s')", corNgsild.geometry);
      return true;
    }
  }

  // § 10.4.2.4 / § 10.4.3.4 — the geometryProperty parameter names the
  // GeoProperty whose value becomes the GeoJSON "geometry" field, so it is
  // only meaningful when the response is GeoJSON. If it is supplied with any
  // other Accept it shall be rejected as 400 BadRequestData.
  if (corRest.out.httpStatusCode < 400 &&
      corNgsild.geometryProperty != NULL &&
      corAcceptParse(corRest.in.accept) != CorMimeGeoJson)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
            "geometryProperty is only valid with Accept: application/geo+json");
    return true;
  }

  // § 5.7.2.4 — Query Entities requires AT LEAST ONE of: type / attrs / q /
  // geoquery (georel+geometry+coordinates) / scopeQ. Filtering by `id` or
  // `idPattern` alone is "too wide query" and shall be 400 BadRequestData.
  // The same applies to Query Temporal Entities (§ 5.7.4.4) and the
  // /entityOperations/query POST variant.
  //
  // Exception: a request paginating via `?entityMap=<id>` is bounded by the
  // already-stored entity map (§ 5.14) — the original selectors lived on the
  // request that built the map, so a continuation request needs no further
  // selector.
  // Verb gate: the minimal-selector check is meaningful only for the
  // URL-param query verbs (GET /entities, GET /temporal/entities).
  // The POST body-based variants (POST /entityOperations/query and
  // POST /temporal/entityOperations/query) carry their selectors in
  // the body, which the parse hook can't translate to corNgsild fields
  // — that happens in the service routine, after this validate hook.
  // Without the verb gate, body-based queries would falsely 400 here.
  uint64_t op = (corRest.serviceP != NULL) ? corRest.serviceP->ldOp : 0;
  if ((op & (LdOpQueryEntities | LdOpQueryTemporal)) &&
      corRest.in.verb == CorVerbGet &&
      corNgsild.entityMapId == NULL)
  {
    bool haveSelector = (corNgsild.type != NULL)
                     || (corNgsild.attrs != NULL)
                     || (corNgsild.q != NULL)
                     || (corNgsild.georel != NULL && corNgsild.geometry != NULL && corNgsild.coordinates != NULL)
                     || (corNgsild.scopeQ != NULL)
                     || (corNgsild.local == true);
    if (!haveSelector)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
              "Query Entities requires at least one of 'type', 'attrs', 'q', a GeoQuery, 'scopeQ', or 'local=true' (§ 5.7.2.4 — id / idPattern alone is too wide)");
      return true;
    }
  }

  return false;
}
