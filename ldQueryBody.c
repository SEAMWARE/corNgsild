//
// FILE            ldQueryBody.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
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

#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjLookup.h"                             // kjLookup
#include "kjson/kjRender.h"                             // kjFastRender
#include "kjson/kjRenderSize.h"                         // kjFastRenderSize

#include "corNgsild/corNgsild.h"                          // ldError, LD_ERROR_*, corNgsild, ldParamHook
#include "corNgsild/LdProblem.h"                         // LD_ERROR_BAD_REQUEST_DATA
#include "corNgsild/ldError.h"                           // ldError
#include "corNgsild/ldQueryBody.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// arrayJoin - comma-join a KjArray of strings.
//
static const char* arrayJoin(KjNode* arrP)
{
  if (arrP == NULL || arrP->type != KjArray)
    return NULL;

  int total = 0;
  int n     = 0;
  for (KjNode* c = arrP->value.firstChildP; c != NULL; c = c->next)
  {
    if (c->type != KjString) continue;
    total += strlen(c->value.s) + 1;
    n++;
  }
  if (n == 0)
    return NULL;

  char* buf = (char*) kaAlloc(&corRest.kalloc, total + 1);
  int pos = 0;
  for (KjNode* c = arrP->value.firstChildP; c != NULL; c = c->next)
  {
    if (c->type != KjString) continue;
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
static void collectFromEntities(KjNode* entsArr)
{
  if (entsArr == NULL || entsArr->type != KjArray)
    return;

  int idLen = 0, typeLen = 0, idCount = 0, typeCount = 0;
  const char* firstIdPattern = NULL;

  for (KjNode* selP = entsArr->value.firstChildP; selP != NULL; selP = selP->next)
  {
    if (selP->type != KjObject) continue;

    KjNode* idP        = kjLookup(selP, "id");
    KjNode* typeP      = kjLookup(selP, "type");
    KjNode* patternP   = kjLookup(selP, "idPattern");

    if (idP != NULL && idP->type == KjString)
    {
      idLen += strlen(idP->value.s) + 1;
      idCount++;
    }
    if (typeP != NULL && typeP->type == KjString)
    {
      typeLen += strlen(typeP->value.s) + 1;
      typeCount++;
    }
    if (firstIdPattern == NULL && patternP != NULL && patternP->type == KjString)
      firstIdPattern = patternP->value.s;
  }

  if (idCount > 0)
  {
    char* buf = (char*) kaAlloc(&corRest.kalloc, idLen + 1);
    int pos = 0;
    for (KjNode* selP = entsArr->value.firstChildP; selP != NULL; selP = selP->next)
    {
      if (selP->type != KjObject) continue;
      KjNode* idP = kjLookup(selP, "id");
      if (idP == NULL || idP->type != KjString) continue;
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
    for (KjNode* selP = entsArr->value.firstChildP; selP != NULL; selP = selP->next)
    {
      if (selP->type != KjObject) continue;
      KjNode* typeP = kjLookup(selP, "type");
      if (typeP == NULL || typeP->type != KjString) continue;
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
static void collectFromGeoQ(KjNode* geoQ)
{
  if (geoQ == NULL || geoQ->type != KjObject)
    return;

  KjNode* georel      = kjLookup(geoQ, "georel");
  KjNode* geometry    = kjLookup(geoQ, "geometry");
  KjNode* coords      = kjLookup(geoQ, "coordinates");
  KjNode* geoproperty = kjLookup(geoQ, "geoproperty");

  if (georel      != NULL && georel->type      == KjString) ldParamHook("georel",      georel->value.s);
  if (geometry    != NULL && geometry->type    == KjString) ldParamHook("geometry",    geometry->value.s);
  if (geoproperty != NULL && geoproperty->type == KjString) ldParamHook("geoproperty", geoproperty->value.s);

  if (coords != NULL)
  {
    int   bufSize = kjFastRenderSize(coords) + 1;
    char* buf     = (char*) kaAlloc(&corRest.kalloc, bufSize);
    kjFastRender(coords, buf);
    ldParamHook("coordinates", buf);
  }
}



// -----------------------------------------------------------------------------
//
// ldQueryBodyToParams -
//
bool ldQueryBodyToParams(KjNode* bodyP)
{
  if (bodyP == NULL || bodyP->type != KjObject)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Not a JSON Object",
            "Query body must be a JSON object");
    return false;
  }

  KjNode* typeP = kjLookup(bodyP, "type");
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
  if (typeP->type != KjString ||
      (strcmp(typeP->value.s, "Query") != 0 &&
       strcmp(typeP->value.s, expandedQuery) != 0))
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Mandatory Field Missing",
            "Query body must carry \"type\": \"Query\"");
    return false;
  }

  for (KjNode* fP = bodyP->value.firstChildP; fP != NULL; fP = fP->next)
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
    if (strcmp(fP->name, "temporalQ") == 0 && fP->type == KjObject)
    {
      for (KjNode* tP = fP->value.firstChildP; tP != NULL; tP = tP->next)
      {
        if (tP->name == NULL || tP->name[0] == '@') continue;

        if (tP->type == KjString)
          ldParamHook(tP->name, tP->value.s);
        else if (tP->type == KjInt)
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

    if (fP->type == KjBoolean)
    {
      ldParamHook(fP->name, fP->value.b ? "true" : "false");
      continue;
    }

    if (fP->type == KjString)
    {
      ldParamHook(fP->name, fP->value.s);
      continue;
    }

    if (fP->type == KjInt)
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
