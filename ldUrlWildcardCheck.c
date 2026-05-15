//
// FILE            ldUrlWildcardCheck.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stddef.h>                                    // NULL
#include <string.h>                                    // strncmp, strstr, strchr

#include "swRest/SwRestState.h"                        // swRest
#include "swRest/SwRestService.h"                      // SwRestService

#include "swNgsild/LdProblem.h"                        // LD_ERROR_BAD_REQUEST_DATA
#include "swNgsild/ldError.h"                          // ldError
#include "swNgsild/ldNameContentCheck.h"               // ldIsValidName
#include "swNgsild/ldUrlWildcardCheck.h"               // Own interface



// -----------------------------------------------------------------------------
//
// isUri - cheap URI shape check: at least one ':' that isn't first/last,
//         and no whitespace anywhere.
//
static bool isUri(const char* s)
{
  if (s == NULL || s[0] == 0)        return false;
  const char* colon = strchr(s, ':');
  if (colon == NULL || colon == s || colon[1] == 0) return false;

  for (const char* p = s; *p != 0; p++)
  {
    if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
      return false;
  }
  return true;
}



// -----------------------------------------------------------------------------
//
// pathHasSegment - does `pattern` contain a literal `/segment/` (or
// `/segment` at the end)?
//
static bool pathHasSegment(const char* pattern, const char* segment)
{
  if (pattern == NULL || segment == NULL) return false;
  size_t segLen = strlen(segment);

  const char* p = pattern;
  while ((p = strchr(p, '/')) != NULL)
  {
    p++;
    if (strncmp(p, segment, segLen) == 0 && (p[segLen] == '/' || p[segLen] == 0))
      return true;
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// slot0NeedsUri - the resource-id paths whose first wildcard must be a URI.
// /types/*, /attributes/*, /jsonldContexts/* etc. carry names or context-id
// URLs that we don't reject here.
//
static bool slot0NeedsUri(const char* sp)
{
  return
    (strncmp(sp, "/ngsi-ld/v1/entities/",            21) == 0) ||
    (strncmp(sp, "/ngsi-ld/v1/temporal/entities/",   30) == 0) ||
    (strncmp(sp, "/ngsi-ld/v1/subscriptions/",       26) == 0) ||
    (strncmp(sp, "/ngsi-ld/v1/csourceRegistrations/", 33) == 0) ||
    (strncmp(sp, "/ngsi-ld/v1/csourceSubscriptions/", 33) == 0) ||
    (strncmp(sp, "/ngsi-ld/v1/entityMaps/",          23) == 0);
}



// -----------------------------------------------------------------------------
//
// ldUrlWildcardOptionsInit - swRest service-init hook.
//
// Walks the URL pattern once and stores per-slot validation flags in
// service->options. Subsequent per-request validation in
// ldUrlWildcardCheck() is a small bit-check.
//
void ldUrlWildcardOptionsInit(SwRestService* service)
{
  if (service == NULL || service->url == NULL)         return;
  if (strncmp(service->url, "/ngsi-ld/", 9) != 0)      return;
  if (service->wildcards <= 0)                         return;

  if (slot0NeedsUri(service->url))
    service->options |= LD_WC_URI_AT_0;

  if (service->wildcards >= 2 && pathHasSegment(service->url, "attrs"))
    service->options |= LD_WC_NAME_AT_1;

  if (service->wildcards >= 3)
    service->options |= LD_WC_URI_AT_2;
}



// -----------------------------------------------------------------------------
//
// ldUrlWildcardCheck -
//
bool ldUrlWildcardCheck(void)
{
  if (swRest.serviceP == NULL)                 return true;
  uint64_t opts = swRest.serviceP->options;
  if (opts == 0)                               return true;

  if ((opts & LD_WC_URI_AT_0) && swRest.in.wildcard[0] != NULL
      && !isUri(swRest.in.wildcard[0]))
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Bad Request Data",
            "'%s' is not a valid URI", swRest.in.wildcard[0]);
    return false;
  }

  if ((opts & LD_WC_NAME_AT_1) && swRest.in.wildcard[1] != NULL
      && !ldIsValidName(swRest.in.wildcard[1]))
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Bad Request",
            "invalid attribute name '%s' (§ 4.6.2)", swRest.in.wildcard[1]);
    return false;
  }

  if ((opts & LD_WC_URI_AT_2) && swRest.in.wildcard[2] != NULL
      && !isUri(swRest.in.wildcard[2]))
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Bad Request Data",
            "instanceId '%s' is not a valid URI", swRest.in.wildcard[2]);
    return false;
  }

  return true;
}
