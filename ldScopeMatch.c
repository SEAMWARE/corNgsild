//
// FILE            ldScopeMatch.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                    // bool
#include <stdio.h>                                      // snprintf
#include <string.h>                                     // strcmp, strlen, memcpy

#include "kalloc/kaAlloc.h"                             // kaAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjLookup.h"                             // kjLookup

#include "swNgsild/LdVocab.h"                           // LD_VOCAB_SCOPE, LD_VOCAB_NGSILD_NULL
#include "swNgsild/ldScopeMatch.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// ldScopePatternMatch - check if a scope value matches a scope pattern
//
bool ldScopePatternMatch(const char* pattern, const char* value)
{
  if (strcmp(pattern, "/#") == 0)
    return (value[0] != 0);

  const char* p = pattern;
  const char* v = value;

  while (*p != 0 && *v != 0)
  {
    if (p[0] == '/' && p[1] == '#' && p[2] == 0)
    {
      //
      // Multi-level wildcard at end: match the whole hierarchy below the scope given so far.
      // "Below" starts at a scope level boundary — "/Madrid/G/#" covers "/Madrid/G/Gardens"
      // but not "/Madrid/Gardens", which is a different scope, not a scope below "/Madrid/G".
      //
      return (*v == '/');
    }

    if (*p == '+')
    {
      // Single-level wildcard: skip one path segment in value - and a scope level is never empty
      const char* levelStart = v;

      while (*v != 0 && *v != '/')
        v++;

      if (v == levelStart)
        return false;

      p++;
    }
    else
    {
      if (*p != *v)
        return false;
      p++;
      v++;
    }
  }

  // Check for trailing "/#" in pattern (matches the current level too)
  if (p[0] == '/' && p[1] == '#' && p[2] == 0)
    return true;

  return (*p == 0 && *v == 0);
}



// -----------------------------------------------------------------------------
//
// ldScopeToRegex - convert a scope pattern to a regex string
//
int ldScopeToRegex(const char* pattern, char* buf, int bufSize)
{
  // Special case: "/#" matches any entity with a non-empty scope
  if (strcmp(pattern, "/#") == 0)
    return snprintf(buf, bufSize, ".+");

  char* out    = buf;
  char* outEnd = buf + bufSize - 1;

  *out++ = '^';

  const char* p = pattern;

  while (*p != 0 && out < outEnd)
  {
    // Check for "/#" at end (multi-level wildcard)
    if (p[0] == '/' && p[1] == '#' && p[2] == 0)
    {
      // "(/.*)?$" -- matches the level itself plus any children
      const char* suffix = "(/.*)?$";
      int suffixLen = strlen(suffix);

      if (out + suffixLen < outEnd)
      {
        memcpy(out, suffix, suffixLen);
        out += suffixLen;
      }

      *out = 0;
      return (int)(out - buf);
    }

    if (*p == '+')
    {
      // Single-level wildcard: matches one path segment
      const char* wc = "[^/]+";
      int wcLen = 5;

      if (out + wcLen < outEnd)
      {
        memcpy(out, wc, wcLen);
        out += wcLen;
      }

      p++;
    }
    else if (*p == '.' || *p == '(' || *p == ')' || *p == '[' || *p == ']' || *p == '{' || *p == '}' || *p == '\\' || *p == '^' || *p == '$' || *p == '|' || *p == '?' || *p == '*')
    {
      // Escape regex metacharacters (except '/' which is literal)
      if (out + 2 < outEnd)
      {
        *out++ = '\\';
        *out++ = *p;
      }
      p++;
    }
    else
    {
      *out++ = *p++;
    }
  }

  // Append '$' for exact match
  if (out < outEnd)
    *out++ = '$';

  *out = 0;
  return (int)(out - buf);
}



// -----------------------------------------------------------------------------
//
// scopeValueCanonicalize - give one Scope its implicit leading '/'
//
static void scopeValueCanonicalize(KjNode* valueP, KAlloc* kaP)
{
  if ((valueP->type != KjString) || (valueP->value.s == NULL) || (valueP->value.s[0] == '/'))
    return;

  if (strcmp(valueP->value.s, LD_VOCAB_NGSILD_NULL) == 0)   // the NGSI-LD Null marks a deleted Scope, it is not one
    return;

  int   len     = strlen(valueP->value.s);
  char* slashed = kaAlloc(kaP, len + 2);

  slashed[0] = '/';
  memcpy(&slashed[1], valueP->value.s, len + 1);

  valueP->value.s = slashed;
}



// -----------------------------------------------------------------------------
//
// objectCanonicalize - canonicalize the "scope" member of one Entity/Registration
//
static void objectCanonicalize(KjNode* objectP, KAlloc* kaP)
{
  if (objectP == NULL || objectP->type != KjObject)
    return;

  KjNode* scopeP = kjLookup(objectP, LD_VOCAB_SCOPE);
  if (scopeP == NULL)
    return;

  if (scopeP->type == KjArray)
  {
    for (KjNode* valueP = scopeP->value.firstChildP; valueP != NULL; valueP = valueP->next)
      scopeValueCanonicalize(valueP, kaP);
  }
  else
    scopeValueCanonicalize(scopeP, kaP);
}



// -----------------------------------------------------------------------------
//
// ldScopeCanonicalize -
//
void ldScopeCanonicalize(KjNode* treeP, KAlloc* kaP)
{
  if (treeP == NULL)
    return;

  if (treeP->type == KjObject)
  {
    objectCanonicalize(treeP, kaP);
  }
  else if (treeP->type == KjArray)
  {
    for (KjNode* itemP = treeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
      objectCanonicalize(itemP, kaP);
  }
}
