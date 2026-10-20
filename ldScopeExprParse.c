//
// FILE            ldScopeExprParse.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include <string.h>                                      // strlen, strcmp, memcpy

#include "corAlloc/CorAlloc.h"                         // corAlloc
#include "corAlloc/corAlloc.h"                         // corAlloc
#include "corAlloc/corAllocStrdup.h"                    // corAllocStrdup

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
static bool parseGroup(char* str, LdScopeGroup* group, CorAlloc* faP)
{
  // Count semicolons to determine array size
  int count = 1;

  for (char* p = str; *p != 0; p++)
  {
    if (*p == ';')
      count++;
  }

  group->scopeV = (char**) corAlloc(faP, (count + 1) * sizeof(char*));
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
        char* slashed = (char*) corAlloc(faP, len + 2);

        slashed[0] = '/';
        memcpy(&slashed[1], start, len + 1);

        group->scopeV[ix++] = slashed;
      }
      else
        group->scopeV[ix++] = corAllocStrdup(faP, start);

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
// levelCharOk - a character of a Scope level
//
// § 7.2.5: ScopeQLevel = unicodeLetter *(unicodeNumber / unicodeLetter / "_"). The Scopes of an Entity
// are stored as given, so the level characters are those of ldIsValidName - digits first, '.' and '-'
// too - for a query to reach every Scope that can be stored. Non-ASCII bytes are UTF-8, candidates for
// unicodeLetter / unicodeNumber. What the grammar rules on is the structure: '/', '+', '#', ';', ',',
// '|', '(' and ')' are never level characters.
//
static bool levelCharOk(unsigned char c)
{
  if (c >= 0x80)
    return true;

  if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
    return true;

  return (c == '_' || c == '.' || c == '-');
}



// -----------------------------------------------------------------------------
//
// scopeQPath - ScopeQ = [/] ScopeQLevel *(/ ScopeQLevel), ScopeQLevel being '+' or level characters
//
// Advances *pP past the Scope, stopping before a "/#" that follows it. Returns NULL, or what is wrong.
//
static const char* scopeQPath(const char** pP)
{
  const char* p = *pP;

  // § 5.2.7: the leading '/' of a Scope is implicit (see parseGroup)
  if (*p == '/')
    ++p;

  while (true)
  {
    if (*p == '+')
    {
      ++p;

      if (*p != 0 && levelCharOk((unsigned char) *p))
        return "'+' is a whole scope level - '/+/', not part of one";
    }
    else
    {
      const char* start = p;

      while (*p != 0 && levelCharOk((unsigned char) *p))
        ++p;

      if (p == start)
        return "empty scope level, or a character that the Scope Query Language does not allow in one";
    }

    if (p[0] != '/' || p[1] == '#')
      break;

    ++p;
  }

  *pP = p;
  return NULL;
}



// -----------------------------------------------------------------------------
//
// scopeQSyntax - check a scopeQ against the grammar of § 7.2.5
//
//   ScopesQ  = OrScopeQ *(orOp OrScopeQ)  /  "/#"
//   OrScopeQ = "(" ScopeQ *(";" ScopeQ) ")"  /  ScopeQ ["/#"]
//   orOp     = "|" / ","
//
// "For logical and grouping parenthesis are needed" - an AND outside parentheses is outside the
// grammar, as is a "#" anywhere but in a "/#" at the end of a Scope.
//
// Returns NULL, or what is wrong - the detail of the 400.
//
static const char* scopeQSyntax(const char* value)
{
  if (strcmp(value, "/#") == 0)
    return NULL;

  const char* p = value;
  const char* detail;

  while (true)
  {
    if (*p == '(')
    {
      ++p;

      while (true)
      {
        if ((detail = scopeQPath(&p)) != NULL)
          return detail;

        //
        // "/#" inside an AND group, "(/Madrid/#;/CompanyA)", is outside the grammar, which gives the
        // wildcard to OrScopeQ only. It is accepted: the prose of the clause does not limit it, and an
        // AND of hierarchies is the query a scope tree is made for.
        //
        if (p[0] == '/' && p[1] == '#')
          p += 2;

        if (*p != ';')
          break;

        ++p;
      }

      if (*p != ')')
        return "a parenthesised group is Scopes separated by ';' and closed by ')' - no nesting";

      ++p;
    }
    else
    {
      if ((detail = scopeQPath(&p)) != NULL)
        return detail;

      if (p[0] == '/' && p[1] == '#')
      {
        p += 2;

        if (*p != 0 && *p != ',' && *p != '|')
          return "'#' only as the last level of a Scope: '/A/#'";
      }

      if (*p == ';')
        return "an AND of Scopes needs parentheses: (/A;/B)";
    }

    if (*p == 0)
      return NULL;

    if (*p == '#')
      return "'#' only as the last level of a Scope: '/A/#'";

    if (*p != ',' && *p != '|')
      return "Scopes are separated by ',' or '|' - an AND of them is '(/A;/B)'";

    ++p;
  }
}



// -----------------------------------------------------------------------------
//
// parenthesesBalanced -
//
static bool parenthesesBalanced(const char* value)
{
  int depth = 0;

  for (const char* p = value; *p != 0; p++)
  {
    if      (*p == '(')  depth++;
    else if (*p == ')')  depth--;

    if (depth < 0)
      return false;
  }

  return (depth == 0);
}



// -----------------------------------------------------------------------------
//
// scopeExprParse - parse a scopeQ selection expression
//
// Grammar:
//   ScopesQ    = OrScopeQ *(orOp OrScopeQ)
//   OrScopeQ   = '(' ScopeQ *(';' ScopeQ) ')' | ScopeQ
//   orOp       = '|' / ','
//   ScopeQ     = scope-path with optional '+' (single-level wildcard) and '/#' (multi-level wildcard)
//
// strict: the whole grammar (scopeQSyntax). Without it, only the parenthesis balance - what a scopeQ
// stored before the grammar check was accepted with (ldScopeExprParseStored).
//
static LdScopeExpr* scopeExprParse(const char* value, CorAlloc* faP, bool strict)
{
  if (value == NULL || value[0] == 0)
    return NULL;

  //
  // Syntax
  //
  // Both splitting loops below use the parenthesis depth to tell a top-level OR operator from one
  // inside an AND group, and the second loop ends on the string-terminating zero *at depth zero*.
  // Unbalanced parentheses leave the depth off, and the loop walks past the end of the string,
  // reading whatever follows it in memory.
  //
  const char* detail = NULL;

  if (parenthesesBalanced(value) == false)
    detail = "unbalanced parenthesis in scopeQ expression";
  else if (strict)
    detail = scopeQSyntax(value);

  if (detail != NULL)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid scopeQ parameter", detail);
    return NULL;
  }

  // Work on a copy
  char* buf = corAllocStrdup(faP, value);

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
  LdScopeExpr* expr = (LdScopeExpr*) corAlloc(faP, sizeof(LdScopeExpr));

  expr->groupV     = (LdScopeGroup*) corAlloc(faP, groupCount * sizeof(LdScopeGroup));
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



// -----------------------------------------------------------------------------
//
// ldScopeExprParse - parse a scopeQ of a request: the grammar of § 7.2.5
//
LdScopeExpr* ldScopeExprParse(const char* value, CorAlloc* faP)
{
  return scopeExprParse(value, faP, true);
}



// -----------------------------------------------------------------------------
//
// ldScopeExprParseStored - parse a stored scopeQ (a subscription's, loaded into a cache)
//
// A subscription created before the grammar check may hold a scopeQ outside it - "/A;/B", an AND
// without its parentheses. Refused, it would leave the subscription with no scope filter at all.
//
LdScopeExpr* ldScopeExprParseStored(const char* value, CorAlloc* faP)
{
  return scopeExprParse(value, faP, false);
}
