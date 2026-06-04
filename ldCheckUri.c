//
// FILE            ldCheckUri.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                     // bool
#include <stddef.h>                                      // NULL

#include "swNgsild/LdProblem.h"                          // LD_ERROR_*
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldCheckUri.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// isSchemeChar - valid character in a URI scheme (letter, digit, +, -, .)
//
static bool isSchemeChar(char c)
{
  if (c >= 'a' && c <= 'z')  return true;
  if (c >= 'A' && c <= 'Z')  return true;
  if (c >= '0' && c <= '9')  return true;
  if (c == '+' || c == '-' || c == '.')  return true;

  return false;
}



// -----------------------------------------------------------------------------
//
// uriValid - simple URI validation: must have "scheme:something", no spaces
//
// The scheme must start with a letter and contain only scheme chars (RFC 3986
// § 3.1) - 'entity_pattern=urn:x' has a colon but is NOT a URI.
//
static bool uriValid(const char* uri)
{
  if (uri == NULL || uri[0] == 0)
    return false;

  // Scheme must start with a letter
  if (!((uri[0] >= 'a' && uri[0] <= 'z') || (uri[0] >= 'A' && uri[0] <= 'Z')))
    return false;

  // Find the colon that ends the scheme - only scheme chars before it
  const char* colon = NULL;
  for (const char* p = uri + 1; *p != 0; p++)
  {
    if (*p == ':')
    {
      colon = p;
      break;
    }

    if (isSchemeChar(*p) == false)
      return false;
  }

  if (colon == NULL)
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
