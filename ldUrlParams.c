//
// FILE            ldUrlParams.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdlib.h>                                      // atoi
#include <string.h>                                      // strcmp, strstr, strcasecmp

#include "kalloc/KAlloc.h"                             // kaAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "swRest/swRest.h"                             // swRest
#include "swJsonld/swldDownload.h"                         // swldContextFromUrl
#include "swJsonld/swldExpand.h"                           // swldExpand
#include "swJsonld/swldInit.h"                             // swldCoreContext
#include "swNgsild/ldCheckDateTime.h"                    // ldCheckDateTime, ldIsoToNanoseconds
#include "swNgsild/ldTypes.h"                            // ldFormatFromString
#include "swNgsild/ldQueryParams.h"                      // ldParamSplit, ldParamExpandV
#include "swNgsild/ldQParse.h"                           // ldQParse
#include "swNgsild/LdProblem.h"                          // LD_ERROR_BAD_REQUEST_DATA
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/LdGeoRel.h"                           // ldGeoRelParse
#include "swNgsild/LdScopeExpr.h"                        // ldScopeExprParse
#include "swNgsild/LdTypeExpr.h"                         // ldTypeExprParse
#include "swNgsild/SwNgsild.h"                           // Own interface



// -----------------------------------------------------------------------------
//
// ldLocalOnly - global flag set by --localOnly CLI arg (no distributed ops)
//
bool        ldLocalOnly         = false;
char*       ldDefaultContextUrl  = NULL;
const char* ldCsourceAliasBase   = NULL;



// -----------------------------------------------------------------------------
//
// swNgsild - thread-local per-request state
//
__thread SwNgsild swNgsild;



// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
//
// ldContextResolve - resolve @context from Link header or fall back to core context
//
// Called lazily for GET requests (no payload → parseHook didn't set contextP).
// Also callable directly from service routines that need the context before
// the param hook runs (e.g. GET with zero URL params).
//
void ldContextResolve(void)
{
  if (swNgsild.contextP != NULL)
    return;

  KAlloc* faP = &swRest.kalloc;

  for (int i = 0; i < swRest.in.httpHeaderCount; i++)
  {
    if (strcasecmp(swRest.in.httpHeaderV[i].key, "Link") == 0 &&
        strstr(swRest.in.httpHeaderV[i].value, "json-ld#context") != NULL)
    {
      char* hdr   = swRest.in.httpHeaderV[i].value;
      char* start = strchr(hdr, '<');
      char* end   = (start != NULL) ? strchr(start, '>') : NULL;

      if (start != NULL && end != NULL)
      {
        start++;
        *end = 0;
        swNgsild.contextP = swldContextFromUrl(start, faP);
        *end = '>';
      }

      break;
    }
  }

  if (swNgsild.contextP == NULL)
    swNgsild.contextP = swldCoreContext();
}



// ldParamHook - callback for swRest param validation
//
void ldParamHook(const char* name, const char* value)
{
  KAlloc* faP = &swRest.kalloc;

  //
  // Lazy context resolution
  //
  ldContextResolve();

  if (strcmp(name, "id") == 0)
  {
    swNgsild.id  = (char*) value;
    swNgsild.idV = ldParamSplit((char*) value, faP);
  }
  else if (strcmp(name, "idPattern") == 0)
  {
    swNgsild.idPattern = (char*) value;
  }
  else if (strcmp(name, "type") == 0)
  {
    swNgsild.type     = (char*) value;
    swNgsild.typeExpr = ldTypeExprParse(value, faP);

    // For backward compat: if all groups are simple (single-type), populate flat typeV
    if (swNgsild.typeExpr != NULL && swNgsild.typeExpr->isSimple)
    {
      int n = swNgsild.typeExpr->groupCount;

      swNgsild.typeV = (char**) kaAlloc(faP, (n + 1) * sizeof(char*));

      for (int ix = 0; ix < n; ix++)
        swNgsild.typeV[ix] = swNgsild.typeExpr->groupV[ix].typeV[0];

      swNgsild.typeV[n] = NULL;
    }
  }
  else if (strcmp(name, "datasetId") == 0)
  {
    swNgsild.datasetId  = (char*) value;
    swNgsild.datasetIdV = ldParamSplit((char*) value, faP);
  }
  else if (strcmp(name, "pick") == 0)
  {
    swNgsild.pick  = (char*) value;
    swNgsild.pickV = ldParamSplit((char*) value, faP);  // expanded later in ldExpandParams
  }
  else if (strcmp(name, "omit") == 0)
  {
    swNgsild.omit  = (char*) value;
    swNgsild.omitV = ldParamSplit((char*) value, faP);  // expanded later in ldExpandParams
  }
  else if (strcmp(name, "expandValues") == 0)
  {
    swNgsild.expandValues  = (char*) value;
    swNgsild.expandValuesV = ldParamSplit((char*) value, faP);  // NOT expanded — names used as-is during q-parse
  }
  else if (strcmp(name, "geometryProperty") == 0)
  {
    swNgsild.geometryProperty = (char*) value;
  }
  else if (strcmp(name, "jsonKeys") == 0)
  {
    swNgsild.jsonKeys  = (char*) value;
    swNgsild.jsonKeysV = ldParamSplit((char*) value, faP);  // expanded later in ldExpandParams
  }
  else if (strcmp(name, "scopeQ") == 0)
  {
    swNgsild.scopeQ    = (char*) value;
    swNgsild.scopeExpr = ldScopeExprParse(value, faP);
  }
  else if (strcmp(name, "limit") == 0)
  {
    swNgsild.limit = atoi(value);
  }
  else if (strcmp(name, "offset") == 0)
  {
    swNgsild.offset = atoi(value);
  }
  else if (strcmp(name, "format") == 0)
  {
    swNgsild.format = ldFormatFromString(value);
  }
  else if (strcmp(name, "count") == 0)
  {
    swNgsild.count = (value != NULL && strcmp(value, "true") == 0);
  }
  else if (strcmp(name, "sysAttrs") == 0)
  {
    swNgsild.sysAttrs = (value != NULL && strcmp(value, "true") == 0);
  }
  else if (strcmp(name, "q") == 0)
  {
    swNgsild.q     = (char*) value;
    swNgsild.qExpr = ldQParse(value, faP);
  }
  else if (strcmp(name, "local") == 0)
  {
    swNgsild.local = (value != NULL && strcmp(value, "true") == 0);
  }
  else if (strcmp(name, "details") == 0)
  {
    swNgsild.details = (value != NULL && strcmp(value, "true") == 0);
  }
  else if (strcmp(name, "kind") == 0)
  {
    swNgsild.kind = (char*) value;
  }
  else if (strcmp(name, "options") == 0)
  {
    // Deprecated comma-separated param.  Wrap with commas for safe substring matching:
    //   "keyValues,sysAttrs" => ",keyValues,sysAttrs,"
    int   len    = strlen(value);
    char* wrapped = (char*) kaAlloc(faP, len + 3);  // ',' + value + ',' + '\0'

    wrapped[0] = ',';
    memcpy(wrapped + 1, value, len);
    wrapped[len + 1] = ',';
    wrapped[len + 2] = '\0';

    // Format — only set if ?format= hasn't already set it
    if (swNgsild.format == LdFormatNone)
    {
      if      (strstr(wrapped, ",keyValues,")   != NULL)  swNgsild.format = LdFormatSimplified;
      else if (strstr(wrapped, ",simplified,")  != NULL)  swNgsild.format = LdFormatSimplified;
      else if (strstr(wrapped, ",concise,")     != NULL)  swNgsild.format = LdFormatConcise;
      else if (strstr(wrapped, ",normalized,")  != NULL)  swNgsild.format = LdFormatNormalized;
    }

    if (strstr(wrapped, ",sysAttrs,") != NULL)
      swNgsild.sysAttrs = true;
  }
  else if (strcmp(name, "georel") == 0)
  {
    swNgsild.georel = (char*) value;
    swNgsild.geoRel = ldGeoRelParse(value, faP);
  }
  else if (strcmp(name, "geometry") == 0)
  {
    if (value[0] == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query", "geometry value is empty");
      return;
    }
    if (strcmp(value, "Point")           != 0 && strcmp(value, "MultiPoint")      != 0 &&
        strcmp(value, "LineString")      != 0 && strcmp(value, "MultiLineString") != 0 &&
        strcmp(value, "Polygon")         != 0 && strcmp(value, "MultiPolygon")    != 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query", "unsupported geometry type: '%s'", value);
      return;
    }
    swNgsild.geometry = (char*) value;
  }
  else if (strcmp(name, "coordinates") == 0)
  {
    if (value[0] == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query", "coordinates value is empty");
      return;
    }
    if (value[0] != '[')
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query", "coordinates must be a JSON array");
      return;
    }
    swNgsild.coordinates = (char*) value;
  }
  else if (strcmp(name, "geoproperty") == 0)
  {
    swNgsild.geoproperty = (char*) value;  // expanded later in ldExpandParams
  }
  else if (strcmp(name, "lang") == 0)
  {
    swNgsild.lang = (char*) value;
  }
  else if (strcmp(name, "observedAt") == 0)
  {
    if (ldCheckDateTime(value) < 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid observedAt",
              "observedAt query parameter is not a valid ISO 8601 DateTime: '%s'", value);
      return;
    }
    swNgsild.observedAt   = (char*) value;
    swNgsild.observedAtNs = ldIsoToNanoseconds(value);
  }
}
