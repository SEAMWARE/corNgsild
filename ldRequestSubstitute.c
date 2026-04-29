//
// FILE            ldRequestSubstitute.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stddef.h>                                     // NULL
#include <string.h>                                     // strcmp, strcasecmp

#include "swRest/SwRestState.h"                         // swRest

#include "swNgsild/ldRequestSubstitute.h"               // Own interface



// -----------------------------------------------------------------------------
//
// ldRequestSubstitute -
//
const char* ldRequestSubstitute(const char* key, const char* value)
{
  if (value == NULL || strcmp(value, "urn:ngsi-ld:request") != 0)
    return value;

  if (key == NULL)
    return NULL;

  for (int i = 0; i < swRest.in.httpHeaderCount; i++)
  {
    if (swRest.in.httpHeaderV[i].key != NULL &&
        strcasecmp(swRest.in.httpHeaderV[i].key, key) == 0)
      return swRest.in.httpHeaderV[i].value;
  }

  return NULL;
}
