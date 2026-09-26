//
// FILE            ldQueryParams.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include <stddef.h>                                    // NULL

#include "kalloc/KAlloc.h"                           // KAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kalloc/kaStrdup.h"                           // kaStrdup

#include "corNgsild/ldQueryParams.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// ldParamSplit - split a comma-separated value into a NULL-terminated array
//
// Splits a COPY. The value passed in is the URI parameter's own value, and it
// is read again after the parse: for the pagination Links (rel="next"/"prev")
// and for the query string forwarded to Context Sources. Split in place, every
// list there was cut at its first comma - attrs=a,b went on as attrs=a, and a
// distributed query asked the source for less than the client asked for.
//
char** ldParamSplit(char* csv, KAlloc* faP)
{
  if (csv == NULL || csv[0] == 0)
    return NULL;

  csv = kaStrdup(faP, csv);

  //
  // Count commas to determine array size
  //
  int count = 1;
  for (char* p = csv; *p != 0; p++)
  {
    if (*p == ',')
      count++;
  }

  //
  // Allocate pointer array (count + 1 for NULL terminator)
  //
  char** result = (char**) kaAlloc(faP, (count + 1) * sizeof(char*));

  //
  // Split (the copy) in place
  //
  int ix = 0;
  result[ix++] = csv;

  for (char* p = csv; *p != 0; p++)
  {
    if (*p == ',')
    {
      *p = 0;
      result[ix++] = p + 1;
    }
  }

  result[ix] = NULL;
  return result;
}
