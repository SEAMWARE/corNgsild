//
// FILE            ldCheckUri.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strchr

#include "swNgsild/LdProblem.h"                          // LD_ERROR_*
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldCheckUri.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// uriValid - simple URI validation: must have "scheme:something", no spaces
//
static bool uriValid(const char* uri)
{
  if (uri == NULL || uri[0] == 0)
    return false;

  // Must contain a colon (scheme separator)
  const char* colon = strchr(uri, ':');

  if (colon == NULL || colon == uri)
    return false;

  // Must have something after the colon
  if (colon[1] == 0)
    return false;

  // No spaces allowed
  for (const char* p = uri; *p != 0; p++)
  {
    if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
      return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// ldCheckUri -
//
bool ldCheckUri(const char* uri)
{
  if (uriValid(uri) == false)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid URI", "Not a valid URI: '%s'", uri);
    return false;
  }

  return true;
}
