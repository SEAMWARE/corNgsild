//
// FILE            ldQueryParams.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stddef.h>                                    // NULL
#include <string.h>                                    // strcmp, strchr

#include "kalloc/KAlloc.h"                           // kaAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kalloc/kaStrdup.h"                          // kaStrdup
#include "swRest/swRest.h"                           // swRest
#include "swJsonld/swldExpand.h"                         // swldExpand

#include "swNgsild/SwNgsild.h"                         // swNgsild
#include "swNgsild/ldQueryParams.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// ldQueryParamValue - look up a URL parameter value by name
//
char* ldQueryParamValue(const char* name)
{
  for (int ix = 0; ix < swRest.in.uriParamCount; ix++)
  {
    if (strcmp(swRest.in.uriParamV[ix].key, name) == 0)
      return swRest.in.uriParamV[ix].value;
  }

  return NULL;
}



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



// -----------------------------------------------------------------------------
//
// ldParamExpandV - expand each value in a NULL-terminated string array
//
char** ldParamExpandV(char** srcV, KAlloc* faP)
{
  if (srcV == NULL)
    return NULL;

  //
  // Count entries
  //
  int count = 0;
  while (srcV[count] != NULL)
    count++;

  //
  // Allocate new array
  //
  char** result = (char**) kaAlloc(faP, (count + 1) * sizeof(char*));

  for (int ix = 0; ix < count; ix++)
  {
    char* expanded = swldExpand(swNgsild.contextP, srcV[ix], faP, NULL, NULL);
    result[ix] = (expanded != NULL) ? expanded : kaStrdup(faP, srcV[ix]);
  }

  result[count] = NULL;
  return result;
}
