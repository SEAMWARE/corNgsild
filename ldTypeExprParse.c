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
static char* expandType(const char* name, KAlloc* faP)
{
  char* expanded = swldExpand(swNgsild.contextP, name, faP, NULL, NULL);

  return (expanded != NULL) ? expanded : kaStrdup(faP, name);
}



// -----------------------------------------------------------------------------
//
// parseGroup - parse an AND group (semicolon-separated types, possibly wrapped in parens)
//
// Input: a faP-strdup'd string like "Home;Vehicle" or "Building" (parens already stripped).
// Splits on ';', expands each type, fills group->typeV and group->count.
//
static bool parseGroup(char* str, LdTypeGroup* group, KAlloc* faP)
{
  // Count semicolons to determine array size
  int count = 1;

  for (char* p = str; *p != 0; p++)
  {
    if (*p == ';')
      count++;
  }

  group->typeV = (char**) kaAlloc(faP, (count + 1) * sizeof(char*));
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

      group->typeV[ix++] = expandType(start, faP);

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
LdTypeExpr* ldTypeExprParse(const char* value, KAlloc* faP)
{
  if (value == NULL || value[0] == 0)
    return NULL;

  // Work on a copy
  char* buf = kaStrdup(faP, value);

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
  LdTypeExpr* expr = (LdTypeExpr*) kaAlloc(faP, sizeof(LdTypeExpr));

  expr->groupV     = (LdTypeGroup*) kaAlloc(faP, groupCount * sizeof(LdTypeGroup));
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

      if (parseGroup(groupStr, &expr->groupV[gix], faP) == false)
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
