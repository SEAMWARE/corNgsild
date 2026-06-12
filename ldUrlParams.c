//
// FILE            ldUrlParams.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <regex.h>                                       // regcomp, regfree
#include <stdlib.h>                                      // strtol
#include <string.h>                                      // strcmp, strstr, strcasecmp
#include <errno.h>                                        // errno, ERANGE
#include <limits.h>                                       // INT_MAX

#include "kalloc/KAlloc.h"                             // kaAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kalloc/kaStrdup.h"                           // kaStrdup
#include "swNgsild/LdProj.h"                              // LdProjItem, ldProjectionParse, ldProjectionTopLevelNames
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
bool        ldLocalOnly          = false;
bool        ldSplitEntities      = true;
bool        ldTestConformance    = false;
char*       ldDefaultContextUrl  = NULL;
uint64_t ldDefaultCooldownNs = 30000000000ULL;   // --cooldownMillis (default 30s; 0 disables the default)
const char* ldCsourceAliasBase   = NULL;
long long   ldBrokerStartTimeSec = 0;
const char* ldBrokerHttpEndpoint = NULL;
KjNode*     ldContextSourceExtras = NULL;



// -----------------------------------------------------------------------------
//
// swNgsildFallback - per-thread fallback used when no per-connection swNgsild
// is bound (background threads, or before swRest.userData is set). The live
// per-request state is per-connection (swRest.userData); see SwNgsild.h.
//
__thread SwNgsild swNgsildFallback;



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

  // Default user @context (§ 4 / § 8.2.3): the request carried no @context of
  // its own (no Link header, no body), so fall back to the broker-configured
  // default user context before core. Core still wins term-by-term (it sits
  // last in the chain), so this only adds the user's terms, never overrides.
  if (swNgsild.contextP == NULL && ldDefaultContextUrl != NULL)
    swNgsild.contextP = swldContextFromUrl(ldDefaultContextUrl, faP);

  if (swNgsild.contextP == NULL)
    swNgsild.contextP = swldCoreContext();
}



// parseBool - strict boolean URL param parser
//
// URL params and their values are case-sensitive (RFC 3986 § 6.2.2.1).
// NGSI-LD § 4 specifies boolean URL params as lowercase "true" / "false".
// Anything else (mixed case "True", empty value, "1"/"0", "yes"/"no") is
// invalid → 400 BadRequestData. The previous silent fallback "anything
// not 'true' is false" masked client bugs — e.g. Robot Framework's
// ${True} serialises as the string "True", which would silently disable
// `local=True` and turn a local-only retrieve into a distributed forward.
//
static bool parseBool(const char* name, const char* value, bool* outP)
{
  if (value != NULL && strcmp(value, "true") == 0)
  {
    *outP = true;
    return true;
  }
  if (value != NULL && strcmp(value, "false") == 0)
  {
    *outP = false;
    return true;
  }

  ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
          "'%s' must be 'true' or 'false' (lowercase); got '%s'",
          name, value != NULL ? value : "");
  return false;
}



// -----------------------------------------------------------------------------
//
// intParam - parse + validate an integer URL param (single-param validation)
//
// Strict: rejects non-numeric values (atoi would silently take "100G" as 100
// and "abc" as 0) and out-of-range values. `minVal` is the smallest accepted
// value (0 for non-negative params, 1 for strictly-positive ones). On error it
// sets the 400 and returns false; on success it writes the parsed int.
//
static bool intParam(const char* name, const char* value, int* outP, int minVal)
{
  char* end = NULL;
  errno = 0;
  long  v   = (value != NULL) ? strtol(value, &end, 10) : 0;

  if ((value == NULL) || (end == value) || (*end != 0) || (errno == ERANGE) || (v < minVal) || (v > INT_MAX))
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
            "'%s' must be a %s integer (got '%s')", name,
            (minVal <= 0) ? "non-negative" : "positive", value != NULL ? value : "");
    return false;
  }

  *outP = (int) v;
  return true;
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
    // § 4.1 / § 5.7.2.4 — each id MUST be a URI. Reject up front
    // so the rest of the request doesn't quietly return an empty
    // result for "invalidUri".
    swNgsild.id  = (char*) value;
    swNgsild.idV = ldParamSplit((char*) value, faP);
    if (swNgsild.idV != NULL)
    {
      for (int i = 0; swNgsild.idV[i] != NULL; i++)
      {
        const char* s     = swNgsild.idV[i];
        const char* colon = (s != NULL) ? strchr(s, ':') : NULL;
        bool ok = (s != NULL && s[0] != 0 && colon != NULL && colon != s && colon[1] != 0);
        if (ok)
        {
          for (const char* p = s; *p != 0 && ok; p++)
            if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
              ok = false;
        }
        if (!ok)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Bad Request",
                  "'%s' is not a valid URI", s);
          return;
        }
      }
    }
  }
  else if (strcmp(name, "idPattern") == 0)
  {
    // § 4.1 / § 5.7.2.4 — idPattern is a POSIX regex. Compile-test
    // it now so a syntax error surfaces as 400 BadRequestData here,
    // not as a 500 from the DB layer when it tries to use it.
    regex_t re;
    int rc = regcomp(&re, value, REG_EXTENDED | REG_NOSUB);
    if (rc != 0)
    {
      char errBuf[128];
      regerror(rc, &re, errBuf, sizeof(errBuf));
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Bad Request",
              "'idPattern' is not a valid regex: %s", errBuf);
      return;
    }
    regfree(&re);
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
    // § 4.21 NGSI-LD Attribute Projection Language. Parse with brace
    // awareness so `pick=id,locatedAt{id,name}` builds a nested tree.
    // pickV[] is derived from the top level for back-compat with code
    // that still walks a flat array.
    const char* errMsg = NULL;
    char*       valCopy = kaStrdup(faP, value);   // parser writes NULs in-place
    swNgsild.pick     = (char*) value;
    swNgsild.pickTree = ldProjectionParse(valCopy, faP, &errMsg);
    if (errMsg != NULL)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Bad Request Data",
              "?pick=: %s", errMsg);
      return;
    }
    swNgsild.pickV    = ldProjectionTopLevelNames(swNgsild.pickTree, faP, true);
  }
  else if (strcmp(name, "attrs") == 0)
  {
    // § 5.10.2 CSR Discovery honors `attrs` as a deprecated synonym for pick.
    // On other routes the bitmask LD_PARAMS_* gates whether attrs is allowed.
    swNgsild.attrs  = (char*) value;
    swNgsild.attrsV = ldParamSplit((char*) value, faP);
  }
  else if (strcmp(name, "omit") == 0)
  {
    const char* errMsg = NULL;
    char*       valCopy = kaStrdup(faP, value);
    swNgsild.omit     = (char*) value;
    swNgsild.omitTree = ldProjectionParse(valCopy, faP, &errMsg);
    if (errMsg != NULL)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Bad Request Data",
              "?omit=: %s", errMsg);
      return;
    }
    swNgsild.omitV    = ldProjectionTopLevelNames(swNgsild.omitTree, faP, false);
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
    // § 6.3.10: non-negative. limit=0 is valid only with count=true — that
    // cross-param dependency is checked in ldParamsValidate.
    if (!intParam("limit", value, &swNgsild.limit, 0)) return;
  }
  else if (strcmp(name, "lastN") == 0)
  {
    if (!intParam("lastN", value, &swNgsild.lastN, 1)) return;
  }
  else if (strcmp(name, "firstN") == 0)
  {
    if (!intParam("firstN", value, &swNgsild.firstN, 1)) return;
  }
  else if (strcmp(name, "offsetN") == 0)
  {
    if (!intParam("offsetN", value, &swNgsild.offsetN, 0)) return;
  }
  else if (strcmp(name, "offset") == 0)
  {
    if (!intParam("offset", value, &swNgsild.offset, 0)) return;
  }
  else if (strcmp(name, "format") == 0)
  {
    // § 6.3.18: 'format' must be one of the spec-defined values; an
    // unknown value is InvalidRequest, not silently ignored (which would
    // bury the error behind a downstream "not supported" 422).
    swNgsild.format = ldFormatFromString(value);
    if (swNgsild.format == LdFormatNone && value != NULL && value[0] != 0)
    {
      ldError(400, LD_ERROR_INVALID_REQUEST, "Invalid Request",
              "unknown 'format' value '%s' (expected: normalized, concise, "
              "simplified|keyValues, temporalValues, aggregatedValues)", value);
      return;
    }
  }
  else if (strcmp(name, "count") == 0)
  {
    if (!parseBool("count", value, &swNgsild.count)) return;
  }
  else if (strcmp(name, "sysAttrs") == 0)
  {
    if (!parseBool("sysAttrs", value, &swNgsild.sysAttrs)) return;
  }
  else if (strcmp(name, "q") == 0 || strcmp(name, "csf") == 0)
  {
    // csf (Context Source Filter) is q-shaped — § 5.10.2 / § 4.9 — and
    // semantically the CSR-discovery flavour of q. Parse into the same
    // field; the discovery service routine can use either name.
    swNgsild.q     = (char*) value;
    swNgsild.qExpr = ldQParse(value, faP);
  }
  else if (strcmp(name, "local") == 0)
  {
    if (!parseBool("local", value, &swNgsild.local)) return;
  }
  else if (strcmp(name, "noForward") == 0)
  {
    if (!parseBool("noForward", value, &swNgsild.noForward)) return;
  }
  else if (strcmp(name, "hops") == 0)
  {
    if (value != NULL)
    {
      swNgsild.hops    = atoi(value);
      swNgsild.hopsSet = true;
      if (swNgsild.hops < 0) swNgsild.hops = 0;
    }
  }
  else if (strcmp(name, "drop") == 0)
  {
    if (value != NULL)
      swNgsild.dropV = ldParamSplit((char*) value, faP);
  }
  else if (strcmp(name, "keep") == 0)
  {
    if (value != NULL)
      swNgsild.keepV = ldParamSplit((char*) value, faP);
  }
  else if (strcmp(name, "details") == 0)
  {
    if (value == NULL || (strcmp(value, "true") != 0 && strcmp(value, "false") != 0))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
              "'details' must be 'true' or 'false'");
      return;
    }
    swNgsild.details = (strcmp(value, "true") == 0);
  }
  else if (strcmp(name, "deleteAll") == 0)
  {
    // RFC 3986 § 6.2.2.1: URL params + values are case-sensitive. NGSI-LD
    // § 4 specifies booleans lowercase. Client tooling that ships "True"
    // (Python requests with bool, Robot Framework ${True}) is the client's
    // bug — surfacing it as 400 here means it gets fixed instead of
    // silently behaving wrong.
    if (!parseBool("deleteAll", value, &swNgsild.deleteAll)) return;
  }
  else if (strcmp(name, "entityMap") == 0)
  {
    if (value != NULL && strcmp(value, "true") == 0)
      swNgsild.entityMapCreate = true;
    else if (value != NULL && value[0] != 0 && strcmp(value, "false") != 0)
      swNgsild.entityMapId = (char*) value;   // URI of existing map
  }
  else if (strcmp(name, "splitEntities") == 0)
  {
    swNgsild.splitEntitiesSet = true;
    if (!parseBool("splitEntities", value, &swNgsild.splitEntitiesVal)) return;
  }
  else if (strcmp(name, "kind") == 0)
  {
    // § 5.13.3: only "Hosted", "Cached", "ImplicitlyCreated" are valid.
    if (value == NULL ||
        (strcmp(value, "Hosted")            != 0 &&
         strcmp(value, "Cached")            != 0 &&
         strcmp(value, "ImplicitlyCreated") != 0))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
              "'kind' must be 'Hosted', 'Cached' or 'ImplicitlyCreated'");
      return;
    }
    swNgsild.kind = (char*) value;
  }
  else if (strcmp(name, "join") == 0)
  {
    // § 4.5.23 — accepted values are "flat", "inline", "@none". Anything
    // else is BadRequestData. "@none" parses but stays semantically inert
    // (no relationship walk).
    if (value == NULL ||
        (strcmp(value, "flat")   != 0 &&
         strcmp(value, "inline") != 0 &&
         strcmp(value, "@none")  != 0))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
              "'join' must be 'flat', 'inline' or '@none'");
      return;
    }
    swNgsild.join = (char*) value;
  }
  else if (strcmp(name, "joinLevel") == 0)
  {
    // § 4.5.23 — strictly positive integer. 0 / negatives / non-numeric
    // are BadRequestData (the spec implies "depth", which is meaningless
    // at zero).
    if (value == NULL)
      return;

    int n = atoi(value);
    if (n < 1)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
              "'joinLevel' must be a positive integer");
      return;
    }
    swNgsild.joinLevel = n;
  }
  else if (strcmp(name, "containedBy") == 0)
  {
    // § 5.7.1.4 — entity ids already encountered while traversing the
    // entity graph. Empty array is explicitly forbidden.
    if (value == NULL || value[0] == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
              "'containedBy' shall not be empty");
      return;
    }

    swNgsild.containedByV = ldParamSplit((char*) value, faP);

    int n = 0;
    if (swNgsild.containedByV != NULL)
      while (swNgsild.containedByV[n] != NULL) n++;
    if (n == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
              "'containedBy' shall not be empty");
      return;
    }
    swNgsild.containedByCount = n;
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
      if      (strstr(wrapped, ",keyValues,")        != NULL)  swNgsild.format = LdFormatSimplified;
      else if (strstr(wrapped, ",simplified,")       != NULL)  swNgsild.format = LdFormatSimplified;
      else if (strstr(wrapped, ",concise,")          != NULL)  swNgsild.format = LdFormatConcise;
      else if (strstr(wrapped, ",normalized,")       != NULL)  swNgsild.format = LdFormatNormalized;
      else if (strstr(wrapped, ",temporalValues,")   != NULL)  swNgsild.format = LdFormatTemporalValues;
      else if (strstr(wrapped, ",aggregatedValues,") != NULL)  swNgsild.format = LdFormatAggregatedValues;
    }

    if (strstr(wrapped, ",sysAttrs,") != NULL)
      swNgsild.sysAttrs = true;

    if (strstr(wrapped, ",noOverwrite,") != NULL)
      swNgsild.noOverwrite = true;

    if (strstr(wrapped, ",update,") != NULL)
      swNgsild.upsertUpdate = true;

    // § 6.3.18: validate every comma-separated token; an unknown one is
    // InvalidRequest, not silently ignored.
    // Tokens spec'd across § 6.3.18 / batch ops § 5.6.7-11 / temporal § 5.7.3+ /
    // pagination § 5.5.4 / TRoE § 5.5.5. Per-route applicability is enforced
    // downstream — this list only rejects truly unknown tokens (typos / malicious
    // input) per spec's "InvalidRequest" rather than silently dropping them.
    static const char* validOptions[] = {
      "keyValues", "simplified", "concise", "normalized",
      "temporalValues", "aggregatedValues",
      "sysAttrs",
      "noOverwrite",
      "update", "replace",
      "count",
      "audit",
      NULL
    };
    char* tok = (char*) value;
    while (tok != NULL && *tok != 0)
    {
      char* end   = strchr(tok, ',');
      int   tlen  = (end != NULL) ? (int)(end - tok) : (int) strlen(tok);
      bool  known = false;
      for (int i = 0; validOptions[i] != NULL; i++)
      {
        int vl = (int) strlen(validOptions[i]);
        if (vl == tlen && strncmp(tok, validOptions[i], tlen) == 0) { known = true; break; }
      }
      if (!known)
      {
        ldError(400, LD_ERROR_INVALID_REQUEST, "Invalid Request",
                "unknown 'options' token '%.*s'", tlen, tok);
        return;
      }
      tok = (end != NULL) ? end + 1 : NULL;
    }
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
  else if (strcmp(name, "timerel") == 0)
  {
    if (strcmp(value, "before") != 0 && strcmp(value, "after") != 0 && strcmp(value, "between") != 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid temporal query",
              "timerel must be 'before', 'after', or 'between' (got '%s')", value);
      return;
    }
    swNgsild.timerel = (char*) value;
  }
  else if (strcmp(name, "timeAt") == 0)
  {
    if (ldCheckDateTime(value) < 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid temporal query",
              "timeAt is not a valid ISO 8601 DateTime: '%s'", value);
      return;
    }
    swNgsild.timeAt   = (char*) value;
    swNgsild.timeAtNs = ldIsoToNanoseconds(value);
  }
  else if (strcmp(name, "endTimeAt") == 0)
  {
    if (ldCheckDateTime(value) < 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid temporal query",
              "endTimeAt is not a valid ISO 8601 DateTime: '%s'", value);
      return;
    }
    swNgsild.endTimeAt   = (char*) value;
    swNgsild.endTimeAtNs = ldIsoToNanoseconds(value);
  }
  else if (strcmp(name, "timeproperty") == 0)
  {
    if (strcmp(value, "observedAt") != 0 && strcmp(value, "createdAt") != 0 &&
        strcmp(value, "modifiedAt") != 0 && strcmp(value, "deletedAt") != 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid temporal query",
              "timeproperty must be observedAt, createdAt, modifiedAt or deletedAt (got '%s')", value);
      return;
    }
    swNgsild.timeproperty = (char*) value;
  }
  else if (strcmp(name, "lang") == 0)
  {
    swNgsild.lang = (char*) value;
  }
  else if (strcmp(name, "aggrMethods") == 0)
  {
    swNgsild.aggrMethods  = (char*) value;
    swNgsild.aggrMethodsV = ldParamSplit((char*) value, faP);
  }
  else if (strcmp(name, "aggrPeriodDuration") == 0)
  {
    swNgsild.aggrPeriodDuration = (char*) value;
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
  else if (strcmp(name, "orderBy") == 0)
  {
    swNgsild.orderBy = (char*) value;

    // Parse orderBy: "attr1;desc,attr2" → array of LdOrderTerm
    // Count terms first
    int count = 1;
    for (const char* p = value; *p; p++)
      if (*p == ',') count++;

    LdOrderTerm* terms = (LdOrderTerm*) kaAlloc(faP, (count + 1) * sizeof(LdOrderTerm));
    int ix = 0;

    char* copy = (char*) kaAlloc(faP, strlen(value) + 1);
    strcpy(copy, value);

    char* saveptr = NULL;
    for (char* tok = strtok_r(copy, ",", &saveptr); tok != NULL; tok = strtok_r(NULL, ",", &saveptr))
    {
      // Each token: "attrName" or "attrName;desc" or "attrName;asc"
      char* semi = strchr(tok, ';');
      LdOrderDir dir = LdOrderAsc;

      if (semi != NULL)
      {
        *semi = 0;
        char* dirStr = semi + 1;
        if (strcmp(dirStr, "desc") == 0)
          dir = LdOrderDesc;
      }

      terms[ix].attrName = tok;  // expanded later in ldExpandParams
      terms[ix].dir      = dir;
      ix++;
    }

    swNgsild.orderByV     = terms;
    swNgsild.orderByCount = ix;
  }
  else if (strcmp(name, "collation") == 0)
  {
    swNgsild.collation = (char*) value;
  }
}
