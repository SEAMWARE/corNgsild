//
// FILE            ldTypeExprParse.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <string.h>                                      // strlen, strchr

#include "kalloc/KAlloc.h"                             // kaAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kalloc/kaStrdup.h"                            // kaStrdup
#include "swJsonld/swldExpand.h"                           // swldExpand

#include "swNgsild/LdProblem.h"                          // LD_ERROR_BAD_REQUEST_DATA
#include "swNgsild/SwNgsild.h"                           // swNgsild
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/LdTypeExpr.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// expandType - expand a single type name via the request's @context
//
static char* expandType(const char* name, KAlloc* kaP)
{
  char* expanded = swldExpand(swNgsild.contextP, name, kaP, NULL, NULL);

  return (expanded != NULL) ? expanded : kaStrdup(kaP, name);
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

  group->typeV = (char**) kaAlloc(kaP, (count + 1) * sizeof(char*));
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

  // Work on a copy
  char* buf = kaStrdup(kaP, value);

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
  LdTypeExpr* expr = (LdTypeExpr*) kaAlloc(kaP, sizeof(LdTypeExpr));

  expr->groupV     = (LdTypeGroup*) kaAlloc(kaP, groupCount * sizeof(LdTypeGroup));
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

  return expr;
}
