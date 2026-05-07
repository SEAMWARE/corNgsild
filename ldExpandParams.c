//
// FILE            ldExpandParams.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                   // bool
#include <stddef.h>                                    // NULL
#include <string.h>                                    // strcmp

#include "kalloc/KAlloc.h"                             // KAlloc, kaAlloc
#include "kalloc/kaStrdup.h"                           // kaStrdup
#include "swRest/SwRestState.h"                        // swRest
#include "swJsonld/swldExpand.h"                       // swldExpand
#include "swNgsild/SwNgsild.h"                         // swNgsild
#include "swNgsild/LdProj.h"                           // LdProjItem

#include "swNgsild/ldExpandParams.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// expandString - expand a single string field, return expanded or original
//
// Reserved entity-member names (id / type / scope / @context) are kept as-is:
// the broker stores them under those bare names (not the @id / @type IRIs the
// JSON-LD core context would map them to), so a downstream string compare
// against an attribute name in storage form must match the bare form.
//
// Other NGSI-LD core terms (createdAt / modifiedAt / observedAt / deletedAt /
// type names defined in the core context) don't need an entry here because
// swldInit's coreContextRewriteToShort makes swldExpand return the bare name
// for any core-context term. We only list the four where the core context
// maps to a JSON-LD keyword (`@id`, `@type`, ...) — the rewrite skips those
// to keep the @-keyword bypass paths happy, so the bare-name guarantee has
// to come from this string-compare instead.
//
static bool isReservedMember(const char* s)
{
  return (strcmp(s, "id")       == 0 ||
          strcmp(s, "type")     == 0 ||
          strcmp(s, "scope")    == 0 ||
          strcmp(s, "@context") == 0);
}

static char* expandString(char* s, KAlloc* kaP)
{
  if (s == NULL)
    return NULL;

  if (isReservedMember(s))
    return s;

  char* expanded = swldExpand(swNgsild.contextP, s, kaP, NULL, NULL);
  return (expanded != NULL) ? expanded : s;
}



// -----------------------------------------------------------------------------
//
// expandArray - expand each entry in a NULL-terminated string array in-place
//
static void expandArray(char** v, KAlloc* kaP)
{
  if (v == NULL)
    return;

  for (int i = 0; v[i] != NULL; i++)
    v[i] = expandString(v[i], kaP);
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
  expandArray(swNgsild.attrsV,            kaP);
  expandArray(swNgsild.omitV,             kaP);
  expandArray(swNgsild.jsonKeysV,         kaP);

  // Expand the projection trees recursively so the linked-entity walker can
  // match against expanded IRIs inside nested entities.
  for (LdProjItem* itemP = swNgsild.pickTree; itemP != NULL; itemP = itemP->next)
    itemP->name = expandString(itemP->name, kaP);
  for (LdProjItem* itemP = swNgsild.omitTree; itemP != NULL; itemP = itemP->next)
    itemP->name = expandString(itemP->name, kaP);
  // Recurse into children.
  // Stack-walk both trees with a tiny helper.
  {
    LdProjItem* stack[64];
    int sp = 0;
    if (swNgsild.pickTree != NULL) stack[sp++] = swNgsild.pickTree;
    if (swNgsild.omitTree != NULL) stack[sp++] = swNgsild.omitTree;
    while (sp > 0)
    {
      LdProjItem* level = stack[--sp];
      for (LdProjItem* itemP = level; itemP != NULL; itemP = itemP->next)
      {
        if (itemP->child != NULL)
        {
          for (LdProjItem* c = itemP->child; c != NULL; c = c->next)
            c->name = expandString(c->name, kaP);
          if (sp < (int) (sizeof(stack) / sizeof(stack[0])))
            stack[sp++] = itemP->child;
        }
      }
    }
  }

  swNgsild.geoproperty      = expandString(swNgsild.geoproperty, kaP);
  swNgsild.geometryProperty = expandString(swNgsild.geometryProperty, kaP);

  // orderBy: expand each dot-separated segment of each term's attrName.
  // Store the expanded segments as a separate array (pathSegV) — the joined
  // form is unsafe to walk because expanded IRIs contain dots themselves
  // (`https://uri.etsi.org/...`).
  if (swNgsild.orderByV != NULL)
  {
    for (int i = 0; i < swNgsild.orderByCount; i++)
    {
      char* in = swNgsild.orderByV[i].attrName;
      if (in == NULL)
      {
        swNgsild.orderByV[i].pathSegV = NULL;
        swNgsild.orderByV[i].pathSegN = 0;
        continue;
      }

      // Count segments first
      int   segCount = 1;
      for (char* p = in; *p != 0; p++)
        if (*p == '.') segCount++;

      char** segV = (char**) kaAlloc(kaP, (segCount + 1) * sizeof(char*));
      char*  tmp  = kaStrdup(kaP, in);    // strtok_r mutates
      char*  save;
      int    ix   = 0;
      for (char* tok = strtok_r(tmp, ".", &save); tok != NULL; tok = strtok_r(NULL, ".", &save))
      {
        char* exp = expandString(tok, kaP);
        segV[ix++] = (exp != NULL) ? exp : tok;
      }
      segV[ix] = NULL;
      swNgsild.orderByV[i].pathSegV = segV;
      swNgsild.orderByV[i].pathSegN = ix;

      // Keep attrName as the joined-expanded form for logging; sort uses pathSegV.
      // Recompute joined form for backward compat.
      size_t bound = 16384;
      char*  out   = (char*) kaAlloc(kaP, bound);
      out[0] = 0;
      size_t outLen = 0;
      for (int s = 0; s < ix; s++)
      {
        size_t segLen = strlen(segV[s]);
        if (outLen + segLen + 2 > bound) break;
        if (outLen) out[outLen++] = '.';
        memcpy(out + outLen, segV[s], segLen);
        outLen += segLen;
        out[outLen] = 0;
      }
      swNgsild.orderByV[i].attrName = out;
    }
  }
}
