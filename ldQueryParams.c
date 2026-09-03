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

#include "corNgsild/ldQueryParams.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// ldParamSplit - split a comma-separated value into a NULL-terminated array
//
char** ldParamSplit(char* csv, KAlloc* faP)
{
  if (csv == NULL || csv[0] == 0)
    return NULL;

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
  // Split in place
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
