//
// FILE            ldParamsValidate.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strcmp, strchr

#include "swNgsild/SwNgsild.h"                           // swNgsild
#include "swNgsild/LdProblem.h"                          // LD_ERROR_*
#include "swNgsild/ldError.h"                            // ldError

#include "swNgsild/ldParamsValidate.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// looksLikeUri -
//
// Cheap shape check shared with the path-segment validator (swRest):
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
  if (swNgsild.limit == 0 && swNgsild.count == false)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request", "limit=0 is only valid when count=true");
    return true;
  }

  // § 4.21: pick and omit are alternative projections — listing the same
  // entity member in both is contradictory and shall be 400.
  if (swNgsild.pickV != NULL && swNgsild.omitV != NULL)
  {
    for (int i = 0; swNgsild.pickV[i] != NULL; i++)
    {
      if (nameInArray(swNgsild.pickV[i], swNgsild.omitV))
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
                "'%s' appears in both pick and omit", swNgsild.pickV[i]);
        return true;
      }
    }
  }

  // § 6.3.20 / § 5.10.2: `attrs` is a deprecated synonym for pick. Mixing
  // it with either pick or omit is contradictory.
  if (swNgsild.attrs != NULL && (swNgsild.pick != NULL || swNgsild.omit != NULL))
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
            "?attrs= cannot be combined with ?pick= or ?omit=");
    return true;
  }

  // ?id= URL param: each entry shall be a valid URI (§ 5.7.2.6 wording
  // applied to the array form).
  if (swNgsild.idV != NULL)
  {
    for (int i = 0; swNgsild.idV[i] != NULL; i++)
    {
      if (!looksLikeUri(swNgsild.idV[i]))
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request",
                "'?id': '%s' is not a valid URI", swNgsild.idV[i]);
        return true;
      }
    }
  }

  // § 6.4.3 attrs: list of attribute names. The reserved entity members
  // id/type/scope/@context are not attribute names — passing them in
  // ?attrs= is BadRequestData. (?pick allows them; that's a different
  // language by design.)
  if (swNgsild.attrsV != NULL)
  {
    for (int i = 0; swNgsild.attrsV[i] != NULL; i++)
    {
      const char* a = swNgsild.attrsV[i];
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

  return false;
}
