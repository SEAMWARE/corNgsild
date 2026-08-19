//
// FILE            ldRequestSubstitute.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stddef.h>                                     // NULL
#include <string.h>                                     // strcmp, strcasecmp

#include "corRest/CorRestState.h"                         // corRest

#include "corNgsild/ldRequestSubstitute.h"               // Own interface



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

  for (int i = 0; i < corRest.in.httpHeaderCount; i++)
  {
    if (corRest.in.httpHeaderV[i].key != NULL &&
        strcasecmp(corRest.in.httpHeaderV[i].key, key) == 0)
      return corRest.in.httpHeaderV[i].value;
  }

  return NULL;
}
