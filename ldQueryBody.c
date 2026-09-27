//
// FILE            ldQueryBody.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Query-object translator (§ 5.2.23). See header for rationale.
//
// Scalars are passed through to ldParamHook as strings. Arrays of
// strings are comma-joined. GeoQuery is exploded into its URL-param
// constituents (georel/geometry/coordinates/geoproperty). entities[]
// EntitySelectors are merged into a single ?id= / ?type= / ?idPattern=
// projection — same limitation as the URL form (per-selector
// correlation collapses).
//

#include <stddef.h>                                    // NULL
#include <string.h>                                    // strcmp, strlen, memcpy
#include <stdio.h>                                     // snprintf

#include "corRest/corRest.h"                            // corRest
#include "kalloc/kaAlloc.h"                             // kaAlloc

#include "corTree/CorNode.h"                            // CorNode
#include "corTree/corTreeLookup.h"                      // corTreeLookup
#include "corJson/corJsonRender.h"                      // corJsonFastRender
#include "corJson/corJsonRenderSize.h"                  // corJsonFastRenderSize

#include "corNgsild/corNgsild.h"                          // ldError, LD_ERROR_*, corNgsild, ldParamHook
#include "corNgsild/LdProblem.h"                         // LD_ERROR_BAD_REQUEST_DATA
#include "corNgsild/ldError.h"                           // ldError
#include "corNgsild/ldQueryBody.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// arrayJoin - comma-join a CorArray of strings.
//
static const char* arrayJoin(CorNode* arrP)
{
  if (arrP == NULL || arrP->type != CorArray)
    return NULL;

  int total = 0;
  int n     = 0;
  for (CorNode* c = arrP->value.firstChildP; c != NULL; c = c->next)
  {
    if (c->type != CorString) continue;
    total += strlen(c->value.s) + 1;
    n++;
  }
  if (n == 0)
    return NULL;

  char* buf = (char*) kaAlloc(&corRest.kalloc, total + 1);
  int pos = 0;
  for (CorNode* c = arrP->value.firstChildP; c != NULL; c = c->next)
  {
    if (c->type != CorString) continue;
    if (pos > 0) buf[pos++] = ',';
    int len = strlen(c->value.s);
    memcpy(buf + pos, c->value.s, len);
    pos += len;
  }
  buf[pos] = 0;
  return buf;
}



// -----------------------------------------------------------------------------
//
// collectFromEntities - pull id/idPattern/type out of EntitySelector[].
//
// Multiple selectors merge: all ids joined, all types joined, first
// idPattern wins. Per-selector correlation is lost (same as URL form).
//
static void collectFromEntities(CorNode* entsArr)
{
  if (entsArr == NULL || entsArr->type != CorArray)
    return;

  int idLen = 0, typeLen = 0, idCount = 0, typeCount = 0;
  const char* firstIdPattern = NULL;

  for (CorNode* selP = entsArr->value.firstChildP; selP != NULL; selP = selP->next)
  {
    if (selP->type != CorObject) continue;

    CorNode* idP       = corTreeLookup(selP, "id");
    CorNode* typeP     = corTreeLookup(selP, "type");
    CorNode* patternP  = corTreeLookup(selP, "idPattern");

    if (idP != NULL && idP->type == CorString)
    {
      idLen += strlen(idP->value.s) + 1;
      idCount++;
    }
    if (typeP != NULL && typeP->type == CorString)
    {
      typeLen += strlen(typeP->value.s) + 1;
      typeCount++;
    }
    if (firstIdPattern == NULL && patternP != NULL && patternP->type == CorString)
      firstIdPattern = patternP->value.s;
  }

  if (idCount > 0)
  {
    char* buf = (char*) kaAlloc(&corRest.kalloc, idLen + 1);
    int pos = 0;
    for (CorNode* selP = entsArr->value.firstChildP; selP != NULL; selP = selP->next)
    {
      if (selP->type != CorObject) continue;
      CorNode* idP = corTreeLookup(selP, "id");
      if (idP == NULL || idP->type != CorString) continue;
      if (pos > 0) buf[pos++] = ',';
      int len = strlen(idP->value.s);
      memcpy(buf + pos, idP->value.s, len);
      pos += len;
    }
    buf[pos] = 0;
    ldParamHook("id", buf);
  }

  if (typeCount > 0)
  {
    char* buf = (char*) kaAlloc(&corRest.kalloc, typeLen + 1);
    int pos = 0;
    for (CorNode* selP = entsArr->value.firstChildP; selP != NULL; selP = selP->next)
    {
      if (selP->type != CorObject) continue;
      CorNode* typeP = corTreeLookup(selP, "type");
      if (typeP == NULL || typeP->type != CorString) continue;
      if (pos > 0) buf[pos++] = ',';
      int len = strlen(typeP->value.s);
      memcpy(buf + pos, typeP->value.s, len);
      pos += len;
    }
    buf[pos] = 0;
    ldParamHook("type", buf);
  }

  if (firstIdPattern != NULL)
    ldParamHook("idPattern", firstIdPattern);
}



// -----------------------------------------------------------------------------
//
// collectFromGeoQ - explode a GeoQuery object (§ 5.2.13).
//
static void collectFromGeoQ(CorNode* geoQ)
{
  if (geoQ == NULL || geoQ->type != CorObject)
    return;

  CorNode* georel     = corTreeLookup(geoQ, "georel");
  CorNode* geometry   = corTreeLookup(geoQ, "geometry");
  CorNode* coords     = corTreeLookup(geoQ, "coordinates");
  CorNode* geoproperty = corTreeLookup(geoQ, "geoproperty");

  if (georel      != NULL && georel->type      == CorString) ldParamHook("georel",     georel->value.s);
  if (geometry    != NULL && geometry->type    == CorString) ldParamHook("geometry",   geometry->value.s);
  if (geoproperty != NULL && geoproperty->type == CorString) ldParamHook("geoproperty", geoproperty->value.s);

  if (coords != NULL)
  {
    int   bufSize = corJsonFastRenderSize(coords) + 1;
    char* buf     = (char*) kaAlloc(&corRest.kalloc, bufSize);
    corJsonFastRender(coords, buf);
    ldParamHook("coordinates", buf);
  }
}



// -----------------------------------------------------------------------------
//
// ldQueryBodyToParams -
//
bool ldQueryBodyToParams(CorNode* bodyP)
{
  if (bodyP == NULL || bodyP->type != CorObject)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Not a JSON Object",
            "Query body must be a JSON object");
    return false;
  }

  CorNode* typeP = corTreeLookup(bodyP, "type");
  if (typeP == NULL)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Mandatory Field Missing",
            "Query body must carry \"type\": \"Query\"");
    return false;
  }

  //
  // The parseHook ran JSON-LD expansion on the body, so "type": "Query"
  // is now either the literal "Query" or the expanded default-vocab IRI.
  // Accept both.
  //
  const char* expandedQuery = "https://uri.etsi.org/ngsi-ld/default-context/Query";
  if (typeP->type != CorString ||
      (strcmp(typeP->value.s, "Query") != 0 &&
       strcmp(typeP->value.s, expandedQuery) != 0))
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Mandatory Field Missing",
            "Query body must carry \"type\": \"Query\"");
    return false;
  }

  for (CorNode* fP = bodyP->value.firstChildP; fP != NULL; fP = fP->next)
  {
    if (fP->name == NULL)                       continue;
    if (fP->name[0] == '@')                     continue;
    if (strcmp(fP->name, "type") == 0)          continue;

    if (strcmp(fP->name, "entities") == 0)
    {
      collectFromEntities(fP);
      continue;
    }

    if (strcmp(fP->name, "geoQ") == 0)
    {
      collectFromGeoQ(fP);
      continue;
    }

    // § 5.2.21 TemporalQuery sub-object — flatten to URL-style params
    // (timerel, timeAt, endTimeAt, lastN, timeproperty, aggrMethods,
    // aggrPeriodDuration). Used by POST /temporal/entityOperations/query
    // (§ 5.7.4 / § 6.24.3.1).
    if (strcmp(fP->name, "temporalQ") == 0 && fP->type == CorObject)
    {
      for (CorNode* tP = fP->value.firstChildP; tP != NULL; tP = tP->next)
      {
        if (tP->name == NULL || tP->name[0] == '@') continue;

        if (tP->type == CorString)
          ldParamHook(tP->name, tP->value.s);
        else if (tP->type == CorInt)
        {
          char buf[32];
          snprintf(buf, sizeof(buf), "%lld", tP->value.i);
          ldParamHook(tP->name, buf);
        }
      }
      continue;
    }

    if (strcmp(fP->name, "attrs")     == 0 ||
        strcmp(fP->name, "pick")      == 0 ||
        strcmp(fP->name, "omit")      == 0 ||
        strcmp(fP->name, "datasetId") == 0)
    {
      const char* joined = arrayJoin(fP);
      if (joined != NULL)
        ldParamHook(fP->name, joined);
      continue;
    }

    if (fP->type == CorBoolean)
    {
      ldParamHook(fP->name, fP->value.b ? "true" : "false");
      continue;
    }

    if (fP->type == CorString)
    {
      ldParamHook(fP->name, fP->value.s);
      continue;
    }

    if (fP->type == CorInt)
    {
      char buf[32];
      snprintf(buf, sizeof(buf), "%lld", fP->value.i);
      ldParamHook(fP->name, buf);
      continue;
    }

    // Anything else (temporalQ, aggrParams, ordering, ...) — not yet
    // wired through URL-param-style handling; silently skipped.
  }

  return true;
}
