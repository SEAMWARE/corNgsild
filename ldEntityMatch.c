//
// FILE            ldEntityMatch.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Entity matching primitives — reusable by both entity queries and
// subscription matching.
//
#include <regex.h>                                     // regcomp, regexec, regfree
#include <stdlib.h>                                    // strtod
#include <string.h>                                    // strcmp

#include "kjson/KjNode.h"                             // KjNode
#include "kjson/kjLookup.h"                           // kjLookup

#include "swNgsild/LdQ.h"                              // LdQNode, LdQTerm
#include "swNgsild/LdVocab.h"                         // LD_VOCAB_*
#include "swNgsild/LdScopeExpr.h"                     // LdScopeExpr
#include "swNgsild/LdTypeExpr.h"                      // LdTypeExpr
#include "swNgsild/ldScopeMatch.h"                     // ldScopePatternMatch
#include "swNgsild/ldEntityMatch.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// entityHasType - check if entity has a specific type (handles both string and array)
//
static bool entityHasType(KjNode* typeP, const char* uri)
{
  if (typeP == NULL)
    return false;

  if (typeP->type == KjString)
    return (strcmp(typeP->value.s, uri) == 0);

  if (typeP->type == KjArray)
  {
    for (KjNode* elemP = typeP->value.firstChildP; elemP != NULL; elemP = elemP->next)
    {
      if (elemP->type == KjString && strcmp(elemP->value.s, uri) == 0)
        return true;
    }
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// ldEntityMatchType -
//
bool ldEntityMatchType(KjNode* typeP, LdTypeExpr* expr)
{
  for (int gix = 0; gix < expr->groupCount; gix++)
  {
    LdTypeGroup* grp      = &expr->groupV[gix];
    bool         allMatch = true;

    for (int tix = 0; tix < grp->count; tix++)
    {
      if (!entityHasType(typeP, grp->typeV[tix]))
      {
        allMatch = false;
        break;
      }
    }

    if (allMatch)
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// entityScopeMatchesPattern -
//
static bool entityScopeMatchesPattern(KjNode* scopeP, const char* pattern)
{
  if (scopeP == NULL)
    return false;

  if (scopeP->type == KjString)
    return ldScopePatternMatch(pattern, scopeP->value.s);

  if (scopeP->type == KjArray)
  {
    for (KjNode* elemP = scopeP->value.firstChildP; elemP != NULL; elemP = elemP->next)
    {
      if (elemP->type == KjString && ldScopePatternMatch(pattern, elemP->value.s))
        return true;
    }
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// ldEntityMatchScope -
//
bool ldEntityMatchScope(KjNode* scopeP, LdScopeExpr* expr)
{
  for (int gix = 0; gix < expr->groupCount; gix++)
  {
    LdScopeGroup* grp      = &expr->groupV[gix];
    bool          allMatch = true;

    for (int six = 0; six < grp->count; six++)
    {
      if (!entityScopeMatchesPattern(scopeP, grp->scopeV[six]))
      {
        allMatch = false;
        break;
      }
    }

    if (allMatch)
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// getAttrValue - find the "value" node for an attribute in an entity
//
static KjNode* getAttrValue(KjNode* entityP, const char* attrName)
{
  if (strcmp(attrName, LD_VOCAB_CREATED_AT)  == 0 ||
      strcmp(attrName, LD_VOCAB_MODIFIED_AT) == 0 ||
      strcmp(attrName, LD_VOCAB_EXPIRES_AT)  == 0)
  {
    return kjLookup(entityP, attrName);
  }

  KjNode* wrapperP = kjLookup(entityP, attrName);
  if (wrapperP == NULL || wrapperP->type != KjObject)
    return NULL;

  // Flat API-shape wrapper: { "type": "Property", "value": X }. Used by
  // CSR regs (§ 5.10.2 q-filter). Try this first; it's harmless when
  // the wrapper is actually the DB-model instance-map shape because
  // that shape has no direct "value" child.
  KjNode* flatValueP = kjLookup(wrapperP, "value");
  if (flatValueP != NULL)
    return flatValueP;

  // DB-model nested shape: wrapper.firstChild → instance object → "value"
  KjNode* instP = wrapperP->value.firstChildP;
  if (instP == NULL || instP->type != KjObject)
    return NULL;

  return kjLookup(instP, "value");
}



// -----------------------------------------------------------------------------
//
// matchTerm - evaluate a single LdQTerm against an entity
//
static bool matchTerm(KjNode* entityP, LdQTerm* term)
{
  if (term->op == LdQExists)
  {
    KjNode* wrapperP = kjLookup(entityP, term->attr);
    return (wrapperP != NULL);
  }

  KjNode* valueP = getAttrValue(entityP, term->attr);
  if (valueP == NULL)
    return false;

  double entityNum = 0;
  bool   isNum     = false;

  if (valueP->type == KjInt)        { entityNum = (double) valueP->value.i; isNum = true; }
  else if (valueP->type == KjFloat) { entityNum = valueP->value.f;          isNum = true; }

  switch (term->valueType)
  {
  case LdQNumber:
    if (!isNum) return false;
    switch (term->op)
    {
    case LdQEqual:     return entityNum == term->value.n;
    case LdQUnequal:   return entityNum != term->value.n;
    case LdQGreater:   return entityNum >  term->value.n;
    case LdQLess:      return entityNum <  term->value.n;
    case LdQGreaterEq: return entityNum >= term->value.n;
    case LdQLessEq:    return entityNum <= term->value.n;
    default:           return false;
    }

  case LdQString:
    if (term->op == LdQPattern || term->op == LdQNotPattern)
    {
      if (valueP->type != KjString) return false;
      regex_t re;
      if (regcomp(&re, term->value.s, REG_EXTENDED | REG_NOSUB) != 0)
        return false;
      bool m = (regexec(&re, valueP->value.s, 0, NULL, 0) == 0);
      regfree(&re);
      return (term->op == LdQPattern) ? m : !m;
    }
    if (valueP->type != KjString) return false;
    {
      int cmp = strcmp(valueP->value.s, term->value.s);
      switch (term->op)
      {
      case LdQEqual:     return cmp == 0;
      case LdQUnequal:   return cmp != 0;
      case LdQGreater:   return cmp >  0;
      case LdQLess:      return cmp <  0;
      case LdQGreaterEq: return cmp >= 0;
      case LdQLessEq:    return cmp <= 0;
      default:           return false;
      }
    }

  case LdQBool:
    if (valueP->type != KjBoolean) return false;
    {
      bool entityBool = valueP->value.b;
      switch (term->op)
      {
      case LdQEqual:   return entityBool == term->value.b;
      case LdQUnequal: return entityBool != term->value.b;
      default:         return false;
      }
    }

  case LdQDateTime:
    if (valueP->type != KjInt) return false;
    {
      long long entityNs = valueP->value.i;
      long long queryNs  = term->value.ns;
      switch (term->op)
      {
      case LdQEqual:     return entityNs == queryNs;
      case LdQUnequal:   return entityNs != queryNs;
      case LdQGreater:   return entityNs >  queryNs;
      case LdQLess:      return entityNs <  queryNs;
      case LdQGreaterEq: return entityNs >= queryNs;
      case LdQLessEq:    return entityNs <= queryNs;
      default:           return false;
      }
    }

  case LdQNoValue:
    if (term->op == LdQPattern && valueP->type == KjString)
    {
      regex_t re;
      if (regcomp(&re, term->value.s, REG_EXTENDED | REG_NOSUB) == 0)
      {
        bool m = (regexec(&re, valueP->value.s, 0, NULL, 0) == 0);
        regfree(&re);
        return m;
      }
    }
    else if (term->op == LdQNotPattern && valueP->type == KjString)
    {
      regex_t re;
      if (regcomp(&re, term->value.s, REG_EXTENDED | REG_NOSUB) == 0)
      {
        bool m = (regexec(&re, valueP->value.s, 0, NULL, 0) != 0);
        regfree(&re);
        return m;
      }
    }
    return false;

  case LdQRange:
    if (!isNum) return false;
    if (term->op == LdQEqual)
      return entityNum >= term->value.numRange.lo && entityNum <= term->value.numRange.hi;
    else if (term->op == LdQUnequal)
      return entityNum < term->value.numRange.lo || entityNum > term->value.numRange.hi;
    return false;

  case LdQValueList:
    for (int i = 0; i < term->value.list.count; i++)
    {
      if (term->value.list.itemType == LdQNumber && isNum)
      {
        double listNum = strtod(term->value.list.values[i], NULL);
        if (entityNum == listNum)
          return (term->op == LdQEqual);
      }
      else if (term->value.list.itemType == LdQString && valueP->type == KjString)
      {
        if (strcmp(valueP->value.s, term->value.list.values[i]) == 0)
          return (term->op == LdQEqual);
      }
    }
    return (term->op == LdQUnequal);

  default:
    return false;
  }
}



// -----------------------------------------------------------------------------
//
// ldEntityMatchQ -
//
bool ldEntityMatchQ(KjNode* entityP, LdQNode* node)
{
  if (node == NULL)
    return true;

  if (node->type == LdQTermNode)
    return matchTerm(entityP, &node->term);

  if (node->type == LdQAndNode)
  {
    for (int i = 0; i < node->group.count; i++)
    {
      if (!ldEntityMatchQ(entityP, node->group.childV[i]))
        return false;
    }
    return true;
  }

  if (node->type == LdQOrNode)
  {
    for (int i = 0; i < node->group.count; i++)
    {
      if (ldEntityMatchQ(entityP, node->group.childV[i]))
        return true;
    }
    return false;
  }

  return false;
}
