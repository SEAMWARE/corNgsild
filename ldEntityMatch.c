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
  if (wrapperP == NULL)
    return NULL;

  // Simplified scalar — CSR user-Properties are always in simplified form per § 5.2.9 (a top-level
  // `csourceProperty1: "aValue"` is the wire shape; no NGSI-LD Property wrapper). § 5.10.2.4 `?q=` filters
  // on these directly. Treat the scalar as the value itself.
  if (wrapperP->type != KjObject)
    return wrapperP;

  // Flat API-shape wrapper: { "type": "Property", "value": X }. Try this first; it's harmless when
  // the wrapper is actually the DB-model instance-map shape because that shape has no direct "value" child.
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
// attrInstanceOf - resolve an attribute's instance object inside `containerP`
//
// The object carrying "value"/"object" + sub-attributes — handles both
// the flat API shape ({type, value, sub: {...}}) and the DB-model
// instance-map shape (wrapper.firstChild → instance object). Returns the
// raw wrapper for scalars (simplified form) and NULL when absent.
//
static KjNode* attrInstanceOf(KjNode* containerP, const char* attrName)
{
  KjNode* wrapperP = kjLookup(containerP, attrName);
  if (wrapperP == NULL || wrapperP->type != KjObject)
    return wrapperP;

  if (kjLookup(wrapperP, "value") != NULL || kjLookup(wrapperP, "object") != NULL)
    return wrapperP;   // flat API shape

  KjNode* instP = wrapperP->value.firstChildP;   // DB-model: first instance
  if (instP != NULL && instP->type == KjObject)
    return instP;

  return wrapperP;
}



// qLeafCompare - compare a fully-resolved value node against a term's operator
// (forward declaration; defined right after matchTerm).
static bool qLeafCompare(LdQTerm* term, KjNode* valueP);



// -----------------------------------------------------------------------------
//
// matchTerm - evaluate a single LdQTerm against an entity
//
static bool matchTerm(KjNode* entityP, LdQTerm* term)
{
  //
  // System temporal attributes (createdAt / modifiedAt) are stored as top-level
  // integer (nanosecond) fields on the entity, not under attr.@none.value.
  // Resolve and compare them directly. Entity-level only (no sub-path).
  //
  if ((term->subPathN == 0) && (term->valuePathN == 0) &&
      ((strcmp(term->attr, "createdAt") == 0) || (strcmp(term->attr, "modifiedAt") == 0)))
  {
    KjNode* tsP = kjLookup(entityP, term->attr);
    if (term->op == LdQExists)    return (tsP != NULL);
    if (term->op == LdQNotExists) return (tsP == NULL);
    if (tsP == NULL || tsP->type != KjInt || term->valueType != LdQDateTime)
      return false;

    long long e = tsP->value.i;
    long long q = term->value.ns;
    switch (term->op)
    {
    case LdQEqual:     return e == q;
    case LdQUnequal:   return e != q;
    case LdQGreater:   return e >  q;
    case LdQLess:      return e <  q;
    case LdQGreaterEq: return e >= q;
    case LdQLessEq:    return e <= q;
    default:           return false;
    }
  }

  //
  // § 4.9 attrPath — descend through the sub-attribute segments: the
  // value (or existence) under test is the LAST segment's, looked up
  // inside the previous segment's instance object.
  //
  KjNode*     containerP = entityP;
  const char* leafName   = term->attr;

  if (term->subPathN > 0)
  {
    KjNode* instP = attrInstanceOf(entityP, term->attr);
    if (instP == NULL || instP->type != KjObject)
      return false;

    for (int i = 0; i < term->subPathN - 1; i++)
    {
      instP = attrInstanceOf(instP, term->subPathV[i]);
      if (instP == NULL || instP->type != KjObject)
        return false;
    }

    containerP = instP;
    leafName   = term->subPathV[term->subPathN - 1];
  }

  if ((term->op == LdQExists || term->op == LdQNotExists) && term->valuePathN == 0)
  {
    KjNode* wrapperP = kjLookup(containerP, leafName);
    bool    present  = (wrapperP != NULL);
    return (term->op == LdQExists) ? present : !present;
  }

  // A missing attribute / value-path segment makes a not-exists term TRUE and
  // every other term (existence and the comparisons) FALSE.
  KjNode* valueP = getAttrValue(containerP, leafName);
  if (valueP == NULL)
    return (term->op == LdQNotExists);

  //
  // § 4.9 "[...]" — descend INTO the value through opaque member names.
  //
  for (int i = 0; i < term->valuePathN; i++)
  {
    if (valueP->type != KjObject)
      return (term->op == LdQNotExists);

    // § 7.2.3.4 item 5 — "[*]" (no natural language specified) matches across
    // ALL keys of a LanguageProperty's languageMap: the term matches if ANY
    // key's value satisfies the comparison; for the negative operators
    // (!= / notPattern) EVERY key must satisfy it (mirroring the array
    // "no element matches" semantics). A "*" segment is terminal.
    if (strcmp(term->valuePathV[i], "*") == 0)
    {
      if (term->op == LdQExists)    return (valueP->value.firstChildP != NULL);
      if (term->op == LdQNotExists) return (valueP->value.firstChildP == NULL);

      bool negative = (term->op == LdQUnequal) || (term->op == LdQNotPattern);
      for (KjNode* langP = valueP->value.firstChildP; langP != NULL; langP = langP->next)
      {
        bool m = qLeafCompare(term, langP);
        if (negative && !m) return false;   // ALL keys must satisfy != / notPattern
        if (!negative && m) return true;    // ANY key satisfies the positive op
      }
      return negative;   // positive: no key matched → false; negative: all matched → true
    }

    valueP = kjLookup(valueP, term->valuePathV[i]);
    if (valueP == NULL)
      return (term->op == LdQNotExists);
  }

  if (term->op == LdQExists)
    return true;
  if (term->op == LdQNotExists)
    return false;  // the attribute/path resolved, so not-exists is false

  return qLeafCompare(term, valueP);
}



// -----------------------------------------------------------------------------
//
// qLeafCompare - compare a fully-resolved value node against a term's operator.
//
// valueP is the value under test (scalar or array); array values use "any
// element matches" for == / pattern / ordering and "no element matches" for
// != / notPattern (§ 4.9). Existence ops are resolved by the caller.
//
static bool qLeafCompare(LdQTerm* term, KjNode* valueP)
{
  double entityNum = 0;
  bool   isNum     = false;

  if (valueP->type == KjInt)        { entityNum = (double) valueP->value.i; isNum = true; }
  else if (valueP->type == KjFloat) { entityNum = valueP->value.f;          isNum = true; }

  switch (term->valueType)
  {
  case LdQNumber:
    // Array value (e.g. a ListProperty valueList of numbers): "any element
    // matches" for ==, "no element matches" for != — the array-containment
    // semantics the mongoc plugin gets natively. Mirrors the LdQString path.
    if (valueP->type == KjArray)
    {
      bool hit = false;
      for (KjNode* elemP = valueP->value.firstChildP; elemP != NULL; elemP = elemP->next)
      {
        double elemNum;
        if      (elemP->type == KjInt)   elemNum = (double) elemP->value.i;
        else if (elemP->type == KjFloat) elemNum = elemP->value.f;
        else continue;
        if (elemNum == term->value.n) { hit = true; break; }
      }
      switch (term->op)
      {
      case LdQEqual:   return hit;
      case LdQUnequal: return !hit;
      default:         return false;   // ordering on an array has no sensible semantic
      }
    }
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
      regex_t re;
      if (regcomp(&re, term->value.s, REG_EXTENDED | REG_NOSUB) != 0)
        return false;
      bool m = false;
      if (valueP->type == KjString)
      {
        m = (regexec(&re, valueP->value.s, 0, NULL, 0) == 0);
      }
      else if (valueP->type == KjArray)
      {
        // VocabProperty.vocab / Property with array value: match if ANY
        // element matches (§ 4.9). Matches BSON's native array-containment
        // semantics that the mongoc plugin gets for free.
        for (KjNode* elemP = valueP->value.firstChildP; elemP != NULL; elemP = elemP->next)
        {
          if (elemP->type == KjString && regexec(&re, elemP->value.s, 0, NULL, 0) == 0)
          { m = true; break; }
        }
      }
      else
      {
        regfree(&re);
        return false;
      }
      regfree(&re);
      return (term->op == LdQPattern) ? m : !m;
    }

    // Equality / ordering. Scalar path identical to before; array path
    // checks "any element matches" for ==, "no element matches" for !=
    // (the spec's array-containment semantics, mirroring BSON $eq/$ne).
    if (valueP->type == KjArray)
    {
      bool hit = false;
      for (KjNode* elemP = valueP->value.firstChildP; elemP != NULL; elemP = elemP->next)
      {
        if (elemP->type == KjString && strcmp(elemP->value.s, term->value.s) == 0)
        { hit = true; break; }
      }
      switch (term->op)
      {
      case LdQEqual:   return hit;
      case LdQUnequal: return !hit;
      default:         return false;   // ordering on array doesn't have a sensible semantic
      }
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
    // Array value (e.g. a ListProperty valueList of booleans): "any element
    // matches" for ==, "no element matches" for !=. Mirrors the LdQString /
    // LdQNumber array paths.
    if (valueP->type == KjArray)
    {
      bool hit = false;
      for (KjNode* elemP = valueP->value.firstChildP; elemP != NULL; elemP = elemP->next)
      {
        if (elemP->type == KjBoolean && elemP->value.b == term->value.b) { hit = true; break; }
      }
      switch (term->op)
      {
      case LdQEqual:   return hit;
      case LdQUnequal: return !hit;
      default:         return false;
      }
    }
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
// findRelationshipTargetId - locate the target id of a named Relationship attr
//
// Walks entityP's attribute containers (storage shape: attrP → datasetId →
// {type, value}) looking for the named relName whose first instance is a
// Relationship; returns the target uri (borrowed) or NULL if absent.
//
static const char* findRelationshipTargetId(KjNode* entityP, const char* relName)
{
  if (entityP == NULL || relName == NULL || entityP->type != KjObject)
    return NULL;

  for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    if (attrP->name == NULL || attrP->type != KjObject)
      continue;
    if (strcmp(attrP->name, relName) != 0)
      continue;

    for (KjNode* instP = attrP->value.firstChildP; instP != NULL; instP = instP->next)
    {
      if (instP->type != KjObject)
        continue;

      KjNode* typeP = kjLookup(instP, "type");
      if (typeP == NULL || typeP->type != KjString)
        continue;
      if (strcmp(typeP->value.s, "Relationship") != 0)
        continue;

      KjNode* valP = kjLookup(instP, "value");
      if (valP != NULL && valP->type == KjString)
        return valP->value.s;
    }
  }
  return NULL;
}



//
// ldEntityMatchQEx -
//
bool ldEntityMatchQEx(KjNode* entityP, LdQNode* node,
                     LdQEntityFetchFunc fetcher, void* userData)
{
  if (node == NULL)
    return true;

  if (node->type == LdQTermNode)
    return matchTerm(entityP, &node->term);

  if (node->type == LdQLinkedNode)
  {
    // Need both a fetcher and a target id; either missing → false
    // (linking entity excluded). Spec § 4.5.23 limits linked retrieval
    // to locally-stored entities or annotated objectType — same gate
    // applies here.
    const char* targetId = findRelationshipTargetId(entityP, node->linked.relName);
    if (targetId == NULL || fetcher == NULL)
      return false;

    KjNode* targetP = NULL;
    if (fetcher(targetId, &targetP, userData) != 0 || targetP == NULL)
      return false;

    return ldEntityMatchQEx(targetP, node->linked.subQ, fetcher, userData);
  }

  if (node->type == LdQAndNode)
  {
    for (int i = 0; i < node->group.count; i++)
    {
      if (!ldEntityMatchQEx(entityP, node->group.childV[i], fetcher, userData))
        return false;
    }
    return true;
  }

  if (node->type == LdQOrNode)
  {
    for (int i = 0; i < node->group.count; i++)
    {
      if (ldEntityMatchQEx(entityP, node->group.childV[i], fetcher, userData))
        return true;
    }
    return false;
  }

  return false;
}



bool ldEntityMatchQ(KjNode* entityP, LdQNode* node)
{
  return ldEntityMatchQEx(entityP, node, NULL, NULL);
}
