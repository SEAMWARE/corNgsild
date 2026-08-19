//
// FILE            ldScopeExprParse.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include <string.h>                                      // strlen, strchr

#include "kalloc/KAlloc.h"                             // kaAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kalloc/kaStrdup.h"                            // kaStrdup

#include "corNgsild/LdProblem.h"                          // LD_ERROR_BAD_REQUEST_DATA
#include "corNgsild/ldError.h"                            // ldError
#include "corNgsild/LdScopeExpr.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// parseGroup - parse an AND group (semicolon-separated scope patterns)
//
// Input: a faP-strdup'd string like "/Madrid/+;/CompanyA" or "/Madrid/#" (parens already stripped).
// Splits on ';', fills group->scopeV and group->count.
//
static bool parseGroup(char* str, LdScopeGroup* group, KAlloc* faP)
{
  // Count semicolons to determine array size
  int count = 1;

  for (char* p = str; *p != 0; p++)
  {
    if (*p == ';')
      count++;
  }

  group->scopeV = (char**) kaAlloc(faP, (count + 1) * sizeof(char*));
  group->count  = count;

  // Split on ';'
  int   ix    = 0;
  char* start = str;

  for (char* p = str; ; p++)
  {
    if (*p == ';' || *p == 0)
    {
      bool end = (*p == 0);

      *p = 0;

      if (start[0] == 0)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid scopeQ parameter", "empty scope pattern in scopeQ expression");
        return false;
      }

      //
      // § 5.2.7 — the leading '/' of a Scope is implicit, and a scope query names Scopes.
      // § 7.2.5's grammar spells the slash out, so a query that has it is untouched; one
      // written the other way, by a client that writes its Entity Scopes the same way,
      // means the very same Scope.
      //
      if (start[0] != '/')
      {
        int   len     = strlen(start);
        char* slashed = (char*) kaAlloc(faP, len + 2);

        slashed[0] = '/';
        memcpy(&slashed[1], start, len + 1);

        group->scopeV[ix++] = slashed;
      }
      else
        group->scopeV[ix++] = kaStrdup(faP, start);

      if (end)
        break;

      start = p + 1;
    }
  }

  group->scopeV[ix] = NULL;
  return true;
}



// -----------------------------------------------------------------------------
//
// ldScopeExprParse - parse a scopeQ selection expression
//
// Grammar:
//   ScopesQ    = OrScopeQ *(orOp OrScopeQ)
//   OrScopeQ   = '(' ScopeQ *(';' ScopeQ) ')' | ScopeQ
//   orOp       = '|' / ','
//   ScopeQ     = scope-path with optional '+' (single-level wildcard) and '/#' (multi-level wildcard)
//
LdScopeExpr* ldScopeExprParse(const char* value, KAlloc* faP)
{
  if (value == NULL || value[0] == 0)
    return NULL;

  //
  // Parenthesis balance
  //
  // Both splitting loops below use the parenthesis depth to tell a top-level OR operator from one
  // inside an AND group, and the second loop ends on the string-terminating zero *at depth zero*.
  // An unbalanced expression leaves the depth off, so without this check the loop walks past the
  // end of the string, reading whatever follows it in memory.
  //
  int depth = 0;

  for (const char* p = value; *p != 0; p++)
  {
    if      (*p == '(')  depth++;
    else if (*p == ')')  depth--;

    if (depth < 0)
      break;
  }

  if (depth != 0)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid scopeQ parameter", "unbalanced parenthesis in scopeQ expression");
    return NULL;
  }

  // Work on a copy
  char* buf = kaStrdup(faP, value);

  //
  // First pass: count OR groups by scanning for '|' and ',' outside parens
  //
  int groupCount = 1;

  depth = 0;

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
  LdScopeExpr* expr = (LdScopeExpr*) kaAlloc(faP, sizeof(LdScopeExpr));

  expr->groupV     = (LdScopeGroup*) kaAlloc(faP, groupCount * sizeof(LdScopeGroup));
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
    else if (*p == 0 || (depth == 0 && (*p == '|' || *p == ',')))
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
