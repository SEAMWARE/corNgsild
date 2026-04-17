//
// FILE            ldExpandParams.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stddef.h>                                    // NULL

#include "kalloc/KAlloc.h"                             // KAlloc, kaAlloc
#include "swRest/SwRestState.h"                        // swRest
#include "swJsonld/swldExpand.h"                       // swldExpand
#include "swNgsild/SwNgsild.h"                         // swNgsild

#include "swNgsild/ldExpandParams.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// expandArray - expand each entry in a NULL-terminated string array in-place
//
static void expandArray(char** v, KAlloc* kaP)
{
  if (v == NULL)
    return;

  for (int i = 0; v[i] != NULL; i++)
  {
    char* expanded = swldExpand(swNgsild.contextP, v[i], kaP, NULL, NULL);
    if (expanded != NULL)
      v[i] = expanded;
  }
}



// -----------------------------------------------------------------------------
//
// expandString - expand a single string field, return expanded or original
//
static char* expandString(char* s, KAlloc* kaP)
{
  if (s == NULL)
    return NULL;

  char* expanded = swldExpand(swNgsild.contextP, s, kaP, NULL, NULL);
  return (expanded != NULL) ? expanded : s;
}



// -----------------------------------------------------------------------------
//
// ldExpandParams - expand all vocab-bearing URL params in swNgsild
//
// Called once per request from the preServiceHook, after the payload body
// has been parsed (@context available) and all URL params stored.
//
// Expands: typeV[], pickV[], omitV[], jsonKeysV[], geoproperty,
//          geometryProperty.
// Does NOT expand: expandValuesV (those names match q-parse tree as-is),
//                  scopeQ, q, datasetId, lang, coordinates.
//
void ldExpandParams(KAlloc* kaP)
{
  if (swRest.out.problemType != NULL)
    return;

  // type: only expand typeV[] entries (the parsed list). swNgsild.type is the
  // raw string (may be "T1,T2") — not meaningful as a single expanded IRI.
  expandArray(swNgsild.typeV,             kaP);
  expandArray(swNgsild.pickV,             kaP);
  expandArray(swNgsild.omitV,             kaP);
  expandArray(swNgsild.jsonKeysV,         kaP);

  swNgsild.geoproperty      = expandString(swNgsild.geoproperty, kaP);
  swNgsild.geometryProperty = expandString(swNgsild.geometryProperty, kaP);

  // orderBy: expand each term's attrName
  if (swNgsild.orderByV != NULL)
  {
    for (int i = 0; i < swNgsild.orderByCount; i++)
      swNgsild.orderByV[i].attrName = expandString(swNgsild.orderByV[i].attrName, kaP);
  }
}
