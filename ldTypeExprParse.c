//
// FILE            ldTypeExprParse.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdlib.h>                                      // malloc, free, calloc
#include <string.h>                                      // strlen, strchr, strdup

#include "kalloc/KAlloc.h"                             // kaAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kalloc/kaStrdup.h"                            // kaStrdup
#include "corJsonld/corLdExpand.h"                           // corLdExpand

#include "corNgsild/LdProblem.h"                          // LD_ERROR_BAD_REQUEST_DATA
#include "corNgsild/CorNgsild.h"                           // corNgsild
#include "corNgsild/ldError.h"                            // ldError
#include "corNgsild/LdTypeExpr.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// memAlloc / memStrdup - allocator shims
//
// The parser is called from two contexts:
//   - request-scoped (URL params, validators) → KAlloc arena, freed
//     automatically at end of request
//   - sub-cache build (parsed tree must outlive any request and any
//     KAlloc arena) → caller passes NULL and the parser uses malloc;
//     ldTypeExprFree releases the tree later.
//
static void* memAlloc(KAlloc* kaP, unsigned long long size)
{
  return (kaP != NULL) ? kaAlloc(kaP, size) : calloc(1, (size_t) size);
}

static char* memStrdup(KAlloc* kaP, const char* s)
{
  return (kaP != NULL) ? kaStrdup(kaP, s) : strdup(s);
}



// -----------------------------------------------------------------------------
//
// expandType - expand a single type name via the request's @context
//
static char* expandType(const char* name, KAlloc* kaP)
{
  char* expanded = corLdExpand(corNgsild.contextP, name, kaP, NULL, NULL);

  if (expanded != NULL)
  {
    // corLdExpand uses kaP; if we're in malloc-mode (kaP == NULL) the
    // returned pointer wouldn't survive a request boundary. Re-strdup
    // it on the heap so the tree is fully malloc-owned.
    return (kaP != NULL) ? expanded : strdup(expanded);
  }

  return memStrdup(kaP, name);
}



// -----------------------------------------------------------------------------
//
// parseGroup - parse an AND group (semicolon-separated types, possibly wrapped in parens)
//
// Input: a kaP-strdup'd string like "Home;Vehicle" or "Building" (parens already stripped).
// Splits on ';', expands each type, fills group->typeV and group->count.
//
static bool parseGroup(char* str, LdTypeGroup* group, KAlloc* kaP)
{
  // Count semicolons to determine array size
  int count = 1;

  for (char* p = str; *p != 0; p++)
  {
    if (*p == ';')
      count++;
  }

  group->typeV = (char**) memAlloc(kaP, (count + 1) * sizeof(char*));
  group->count = count;

  // Split on ';'
  int ix = 0;
  char* start = str;

  for (char* p = str; ; p++)
  {
    if (*p == ';' || *p == 0)
    {
      bool end = (*p == 0);

      *p = 0;

      if (start[0] == 0)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid type parameter", "empty type name in type selection expression");
        return false;
      }

      group->typeV[ix++] = expandType(start, kaP);

      if (end)
        break;

      start = p + 1;
    }
  }

  group->typeV[ix] = NULL;
  return true;
}



// -----------------------------------------------------------------------------
//
// ldTypeExprParse - parse a type selection expression
//
// Grammar:
//   EntityTypes  = OrEntityType *(orOp OrEntityType)
//   OrEntityType = '(' EntityType *(';' EntityType) ')' | EntityType
//   orOp         = '|' / ','
//
LdTypeExpr* ldTypeExprParse(const char* value, KAlloc* kaP)
{
  if (value == NULL || value[0] == 0)
    return NULL;

  // Work on a copy. In malloc-mode we'll free this scratch buffer
  // before returning — it's only used during parsing; the result
  // tree holds independent copies of each leaf type string.
  char* bufOrig = memStrdup(kaP, value);
  char* buf     = bufOrig;

  // § 4.17 allows superfluous outer parens — `(Building|Tower)` is
  // semantically identical to `Building|Tower`. Strip them only when
  // they wrap the WHOLE expression (open at index 0, matching close
  // at the last char with depth never returning to 0 in between) —
  // otherwise `(A;B)|(C;D)` would lose its grouping. Iterate so
  // `((A|B))` collapses fully.
  while (true)
  {
    int len = (int) strlen(buf);
    if (len < 2 || buf[0] != '(' || buf[len - 1] != ')') break;
    int  depth = 0;
    bool wraps = true;
    for (int i = 0; i < len - 1; i++)
    {
      if      (buf[i] == '(') depth++;
      else if (buf[i] == ')') depth--;
      if (depth == 0) { wraps = false; break; }
    }
    if (!wraps) break;
    buf[len - 1] = 0;
    buf++;
  }

  //
  // First pass: count OR groups by scanning for '|' and ',' outside parens
  //
  int groupCount = 1;
  int depth      = 0;

  for (char* p = buf; *p != 0; p++)
  {
    if (*p == '(')       depth++;
    else if (*p == ')')  depth--;
    else if (depth == 0 && (*p == '|' || *p == ','))
      groupCount++;
  }

  //
  // Allocate result
  //
  LdTypeExpr* expr = (LdTypeExpr*) memAlloc(kaP, sizeof(LdTypeExpr));

  expr->groupV     = (LdTypeGroup*) memAlloc(kaP, groupCount * sizeof(LdTypeGroup));
  expr->groupCount = groupCount;
  expr->isSimple   = true;

  //
  // Second pass: split on OR operators outside parens and parse each group
  //
  int   gix   = 0;
  char* start = buf;

  depth = 0;

  for (char* p = buf; ; p++)
  {
    if (*p == '(')       depth++;
    else if (*p == ')')  depth--;
    else if (depth == 0 && (*p == '|' || *p == ',' || *p == 0))
    {
      bool end = (*p == 0);

      *p = 0;

      // Strip surrounding parens if present
      char* groupStr = start;

      if (groupStr[0] == '(')
      {
        groupStr++;

        int len = strlen(groupStr);

        if (len > 0 && groupStr[len - 1] == ')')
          groupStr[len - 1] = 0;
      }

      if (parseGroup(groupStr, &expr->groupV[gix], kaP) == false)
        return NULL;

      if (expr->groupV[gix].count > 1)
        expr->isSimple = false;

      gix++;

      if (end)
        break;

      start = p + 1;
    }
  }

  if (kaP == NULL)
    free(bufOrig);

  return expr;
}



// -----------------------------------------------------------------------------
//
// ldTypeExprFree - release a malloc-mode parsed tree
//
// Only call on trees parsed with kaP == NULL. KAlloc-allocated trees
// are freed automatically when the arena is reset; calling this on
// one would double-free.
//
void ldTypeExprFree(LdTypeExpr* expr)
{
  if (expr == NULL)
    return;

  if (expr->groupV != NULL)
  {
    for (int gix = 0; gix < expr->groupCount; gix++)
    {
      LdTypeGroup* grp = &expr->groupV[gix];
      if (grp->typeV != NULL)
      {
        for (int tix = 0; tix < grp->count; tix++)
          free(grp->typeV[tix]);
        free(grp->typeV);
      }
    }
    free(expr->groupV);
  }

  free(expr);
}
