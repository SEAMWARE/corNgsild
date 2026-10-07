//
// FILE            ldEntityMatch.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Entity matching primitives — reusable by both entity queries and
// subscription matching.
//
#include <regex.h>                                     // regcomp, regexec, regfree
#include <stdlib.h>                                    // strtod
#include <string.h>                                    // strcmp

#include "corTree/CorNode.h"                          // CorNode
#include "corTree/corTreeLookup.h"                    // corTreeLookup

#include "corNgsild/ldAttrTypeDetect.h"                 // ldAttrTypeDetect
#include "corNgsild/LdQ.h"                              // LdQNode, LdQTerm
#include "corNgsild/LdVocab.h"                         // LD_VOCAB_*
#include "corNgsild/LdScopeExpr.h"                     // LdScopeExpr
#include "corNgsild/LdTypeExpr.h"                      // LdTypeExpr
#include "corNgsild/ldScopeMatch.h"                     // ldScopePatternMatch
#include "corNgsild/ldCheckDateTime.h"                  // ldIsoToNanoseconds
#include "corNgsild/ldEntityMatch.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// entityHasType - check if entity has a specific type (handles both string and array)
//
static bool entityHasType(CorNode* typeP, const char* uri)
{
  if (typeP == NULL)
    return false;

  if (typeP->type == CorString)
    return (strcmp(typeP->value.s, uri) == 0);

  if (typeP->type == CorArray)
  {
    for (CorNode* elemP = typeP->value.head; elemP != NULL; elemP = elemP->next)
    {
      if (elemP->type == CorString && strcmp(elemP->value.s, uri) == 0)
        return true;
    }
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// ldEntityMatchType -
//
bool ldEntityMatchType(CorNode* typeP, LdTypeExpr* expr)
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
static bool entityScopeMatchesPattern(CorNode* scopeP, const char* pattern)
{
  if (scopeP == NULL)
    return false;

  if (scopeP->type == CorString)
    return ldScopePatternMatch(pattern, scopeP->value.s);

  if (scopeP->type == CorArray)
  {
    for (CorNode* elemP = scopeP->value.head; elemP != NULL; elemP = elemP->next)
    {
      if (elemP->type == CorString && ldScopePatternMatch(pattern, elemP->value.s))
        return true;
    }
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// ldEntityMatchScope -
//
bool ldEntityMatchScope(CorNode* scopeP, LdScopeExpr* expr)
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
static CorNode* getAttrValue(CorNode* entityP, const char* attrName)
{
  if (strcmp(attrName, LD_VOCAB_CREATED_AT)  == 0 ||
      strcmp(attrName, LD_VOCAB_MODIFIED_AT) == 0 ||
      strcmp(attrName, LD_VOCAB_EXPIRES_AT)  == 0)
  {
    return corTreeLookup(entityP, attrName);
  }

  CorNode* wrapperP = corTreeLookup(entityP, attrName);
  if (wrapperP == NULL)
    return NULL;

  // Simplified scalar — CSR user-Properties are always in simplified form per § 5.2.9 (a top-level
  // `csourceProperty1: "aValue"` is the wire shape; no NGSI-LD Property wrapper). § 5.10.2.4 `?q=` filters
  // on these directly. Treat the scalar as the value itself.
  if (wrapperP->type != CorObject)
    return wrapperP;

  // Flat API-shape wrapper: { "type": "Property", "value": X }. Try this first; it's harmless when
  // the wrapper is actually the DB-model instance-map shape because that shape has no direct "value" child.
  CorNode* flatValueP = corTreeLookup(wrapperP, "value");
  if (flatValueP != NULL)
    return flatValueP;

  // DB-model nested shape: wrapper.firstChild → instance object → "value"
  CorNode* instP = wrapperP->value.head;
  if (instP == NULL || instP->type != CorObject)
    return NULL;

  return corTreeLookup(instP, "value");
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
static CorNode* attrInstanceOf(CorNode* containerP, const char* attrName)
{
  CorNode* wrapperP = corTreeLookup(containerP, attrName);
  if (wrapperP == NULL || wrapperP->type != CorObject)
    return wrapperP;

  if (corTreeLookup(wrapperP, "value") != NULL || corTreeLookup(wrapperP, "object") != NULL)
    return wrapperP;   // flat API shape

  CorNode* instP = wrapperP->value.head;         // DB-model: first instance
  if (instP != NULL && instP->type == CorObject)
    return instP;

  return wrapperP;
}



// -----------------------------------------------------------------------------
//
// Multi-attribute instances (§ 8.5) — clause 7 defines the q "target value" of a
// Property with several instances, when no datasetId is addressed, as "any Value
// of such instances". The matcher used to take wrapperP->value.head and
// compare that one, so only the FIRST instance was ever under test.
//
// These two resolve the instance list instead. Both non-multi shapes count as a
// single instance: the flat API wrapper ({type,value,...}) and the bare scalar
// (a CSR simplified user-Property, § 5.2.9).
//
static int attrInstanceCountOf(CorNode* containerP, const char* attrName)
{
  CorNode* wrapperP = corTreeLookup(containerP, attrName);

  if (wrapperP == NULL)
    return 0;
  if (wrapperP->type != CorObject)
    return 1;                                                  // scalar
  if (corTreeLookup(wrapperP, "value") != NULL || corTreeLookup(wrapperP, "object") != NULL)
    return 1;                                                  // flat API shape

  int n = 0;
  for (CorNode* instP = wrapperP->value.head; instP != NULL; instP = instP->next)
    n++;

  return (n > 0) ? n : 1;
}



static CorNode* attrInstanceAt(CorNode* containerP, const char* attrName, int ix)
{
  CorNode* wrapperP = corTreeLookup(containerP, attrName);

  if (wrapperP == NULL || wrapperP->type != CorObject)
    return wrapperP;
  if (corTreeLookup(wrapperP, "value") != NULL || corTreeLookup(wrapperP, "object") != NULL)
    return wrapperP;

  int i = 0;
  for (CorNode* instP = wrapperP->value.head; instP != NULL; instP = instP->next, i++)
  {
    if (i == ix)
      return (instP->type == CorObject) ? instP : wrapperP;
  }

  return wrapperP;
}



// getAttrValueAt - the ix-th instance's value node (see attrInstanceAt)
//
static CorNode* getAttrValueAt(CorNode* containerP, const char* attrName, int ix)
{
  if (strcmp(attrName, LD_VOCAB_CREATED_AT)  == 0 ||
      strcmp(attrName, LD_VOCAB_MODIFIED_AT) == 0 ||
      strcmp(attrName, LD_VOCAB_EXPIRES_AT)  == 0)
    return corTreeLookup(containerP, attrName);

  CorNode* instP = attrInstanceAt(containerP, attrName, ix);
  if (instP == NULL)
    return NULL;
  if (instP->type != CorObject)
    return instP;                                              // scalar

  CorNode* valueP = corTreeLookup(instP, "value");
  return (valueP != NULL) ? valueP : corTreeLookup(instP, "object");
}



// -----------------------------------------------------------------------------
//
// attrIsRelationshipAt - is the addressed instance a Relationship?
//
// § 7.2.3.3: "If the target element corresponds to a Relationship or
// ListRelationship, the combination of such target element with any operator
// different than equal or unequal shall result in NOT MATCHING."
//
// The target of a Relationship is a URI, and a URI has no order - `r>"mid"` was
// answering on the alphabetical accident that "urn:x:14" sorts after "mid".
// Existence is unaffected: `q=r` asks whether the Attribute is there, not what
// its object compares to.
//
static bool attrIsRelationshipAt(CorNode* containerP, const char* attrName, int ix)
{
  CorNode* instP = attrInstanceAt(containerP, attrName, ix);

  if ((instP == NULL) || (instP->type != CorObject))
    return false;

  LdAttrType attrType = ldAttrTypeDetect(instP);   // the member, or the node's kind (a store's own tree)

  return (attrType == LdAttrRelationship) || (attrType == LdAttrListRelationship);
}



// qLeafCompare - compare a fully-resolved value node against a term's operator
// (forward declaration; defined right after matchTerm).
static bool qLeafCompare(LdQTerm* term, CorNode* valueP);



// -----------------------------------------------------------------------------
//
// matchTerm - evaluate a single LdQTerm against an entity
//
static bool matchTermOnInstance(CorNode* entityP, LdQTerm* term, int instIx)
{
  //
  // System temporal attributes (createdAt / modifiedAt) are stored as top-level
  // integer (nanosecond) fields on the entity, not under attr.@none.value.
  // Resolve and compare them directly. Entity-level only (no sub-path).
  //
  if ((term->subPathN == 0) && (term->valuePathN == 0) &&
      ((strcmp(term->attr, "createdAt") == 0) || (strcmp(term->attr, "modifiedAt") == 0)))
  {
    CorNode* tsP = corTreeLookup(entityP, term->attr);
    if (term->op == LdQExists)    return (tsP != NULL);
    if (term->op == LdQNotExists) return (tsP == NULL);
    if (tsP == NULL || tsP->type != CorInt || term->valueType != LdQDateTime)
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
  CorNode*    containerP = entityP;
  const char* leafName   = term->attr;

  if (term->subPathN > 0)
  {
    // A segment that isn't there means the path as a whole isn't there, so a
    // not-exists term is satisfied — an Entity without 'p' at all certainly
    // does not contain 'p.sub'. Every other term needs the element and fails.
    CorNode* instP = attrInstanceAt(entityP, term->attr, instIx);
    if (instP == NULL || instP->type != CorObject)
      return (term->op == LdQNotExists);

    for (int i = 0; i < term->subPathN - 1; i++)
    {
      instP = attrInstanceOf(instP, term->subPathV[i]);
      if (instP == NULL || instP->type != CorObject)
        return (term->op == LdQNotExists);
    }

    containerP = instP;
    leafName   = term->subPathV[term->subPathN - 1];
  }

  if ((term->op == LdQExists || term->op == LdQNotExists) && term->valuePathN == 0)
  {
    CorNode* wrapperP = corTreeLookup(containerP, leafName);
    bool    present  = (wrapperP != NULL);
    return (term->op == LdQExists) ? present : !present;
  }

  // A missing attribute / value-path segment makes a not-exists term TRUE and
  // every other term (existence and the comparisons) FALSE.
  //
  // instIx addresses the multi-attribute instance only at the TOP level, which
  // is where datasetId lives (§ 8.5: no multi-attribute support below it). Once
  // a sub-path has been walked, containerP is already one instance's object and
  // the leaf is resolved normally.
  CorNode* valueP = (containerP == entityP) ? getAttrValueAt(containerP, leafName, instIx)
                                           : getAttrValue(containerP, leafName);
  if (valueP == NULL)
    return (term->op == LdQNotExists);

  // § 7.2.3.3 - a Relationship answers only to equal and unequal (and to the
  // existence operators, which do not look at the object at all).
  if ((term->op != LdQEqual) && (term->op != LdQUnequal) &&
      (term->op != LdQExists) && (term->op != LdQNotExists) &&
      attrIsRelationshipAt(containerP, leafName, (containerP == entityP) ? instIx : 0))
    return false;

  //
  // § 4.9 "[...]" — descend INTO the value through opaque member names.
  //
  for (int i = 0; i < term->valuePathN; i++)
  {
    if (valueP->type != CorObject)
      return (term->op == LdQNotExists);

    // § 7.2.3.4 item 5 — "[*]" (no natural language specified) matches across
    // ALL keys of a LanguageProperty's languageMap: the term matches if ANY
    // key's value satisfies the comparison; for the negative operators
    // (!= / notPattern) EVERY key must satisfy it (mirroring the array
    // "no element matches" semantics). A "*" segment is terminal.
    if (strcmp(term->valuePathV[i], "*") == 0)
    {
      if (term->op == LdQExists)    return (valueP->value.head != NULL);
      if (term->op == LdQNotExists) return (valueP->value.head == NULL);

      bool negative = (term->op == LdQUnequal) || (term->op == LdQNotPattern);
      for (CorNode* langP = valueP->value.head; langP != NULL; langP = langP->next)
      {
        bool m = qLeafCompare(term, langP);
        if (negative && !m) return false;   // ALL keys must satisfy != / notPattern
        if (!negative && m) return true;    // ANY key satisfies the positive op
      }
      return negative;   // positive: no key matched → false; negative: all matched → true
    }

    valueP = corTreeLookup(valueP, term->valuePathV[i]);
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
// matchTerm - evaluate one q term against EVERY instance of the Attribute
//
// Clause 7: "If a Property has multiple instances (identified by its respective
// datasetId), and no datasetId is explicitly addressed, the target value shall
// be any Value of such instances" (and the same sentence for a Relationship's
// object). Only the first instance used to be looked at, so an Entity whose
// matching value sat on a datasetId instance was simply not found.
//
// Positive operators take ANY instance; the negative ones (!=, notPattern,
// !exists) need EVERY instance to satisfy them, which is the rule this file
// already applies to the two other places where one term faces several
// candidate values: an array value in qLeafCompare, and "[*]" across a
// languageMap. It is also the answer that does not surprise - `speed!=10` on an
// Entity that does have an instance of 10 should not match.
//
static bool matchTerm(CorNode* entityP, LdQTerm* term)
{
  bool negative = (term->op == LdQUnequal) || (term->op == LdQNotPattern) || (term->op == LdQNotExists);
  int  n        = attrInstanceCountOf(entityP, term->attr);

  if (n <= 1)
    return matchTermOnInstance(entityP, term, 0);

  for (int ix = 0; ix < n; ix++)
  {
    bool m = matchTermOnInstance(entityP, term, ix);

    if (negative && !m)  return false;   // ALL instances must satisfy != / notPattern / !exists
    if (!negative && m)  return true;    // ANY instance satisfies a positive operator
  }

  return negative;
}



// -----------------------------------------------------------------------------
//
// qLeafCompare - compare a fully-resolved value node against a term's operator.
//
// valueP is the value under test (scalar or array); array values use "any
// element matches" for == / pattern / ordering and "no element matches" for
// != / notPattern (§ 4.9). Existence ops are resolved by the caller.
//
static bool qLeafCompare(LdQTerm* term, CorNode* valueP)
{
  double entityNum = 0;
  bool   isNum     = false;

  if (valueP->type == CorInt)       { entityNum = (double) valueP->value.i; isNum = true; }
  else if (valueP->type == CorFloat) { entityNum = valueP->value.f;         isNum = true; }

  //
  // § 7.2.3.3, and the asymmetry is the spec's, not a shortcut:
  //
  //   Unequal  - "if the data type of the target value and the data type of the
  //               Query Term value are different, then they shall be considered
  //               UNEQUAL" -> a type mismatch MATCHES.
  //   Greater / Less / Equal
  //            - "if there is no equality between the target value data type and
  //               the Query Term value data type then it shall be considered as
  //               NOT MATCHING".
  //   Pattern / notPattern
  //            - "if the target value data type is different than String then it
  //               shall be considered as NOT MATCHING" - notPattern included, so
  //               a number is not "a value that does not match the regex".
  //
  // Hence every type-guard below answers `term->op == LdQUnequal` rather than
  // plain false: only != survives a mismatch.
  //
  switch (term->valueType)
  {
  case LdQNumber:
    // Array value (e.g. a ListProperty valueList of numbers): "any element
    // matches" for ==, "no element matches" for != — the array-containment
    // semantics the mongoc plugin gets natively. Mirrors the LdQString path.
    if (valueP->type == CorArray)
    {
      bool hit = false;
      for (CorNode* elemP = valueP->value.head; elemP != NULL; elemP = elemP->next)
      {
        double elemNum;
        if      (elemP->type == CorInt)  elemNum = (double) elemP->value.i;
        else if (elemP->type == CorFloat) elemNum = elemP->value.f;
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
    if (!isNum) return (term->op == LdQUnequal);
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
      //
      // A regular expression is matched against a STRING. § 7.2.3.3 says so for
      // both pattern operators: "if the target value data type is different than
      // String then it shall be considered as NOT MATCHING" - and it means
      // notPattern too, so a number is not "a value that does not match /x/",
      // it is a value the operator does not apply to.
      //
      // An array of strings is walked element by element. That is not a
      // ListProperty nicety: a languageMap value may BE an array of strings
      // (§ 5.2.x), and `description[*]~=pain` has to see "pain" inside
      // ["schmerz","pain"] - query_q_langprop_alllang pins it.
      //
      // The guard that was missing: an array with no string in it at all is a
      // target the operator cannot be applied to, so it must not match !~=
      // either. Computing "no element matched" and negating it reported a
      // NUMERIC array as "does not match the pattern", which it does not - it
      // has no string to match or fail against.
      //
      bool comparable = (valueP->type == CorString);

      if (valueP->type == CorArray)
      {
        for (CorNode* elemP = valueP->value.head; elemP != NULL; elemP = elemP->next)
        {
          if (elemP->type == CorString) { comparable = true; break; }
        }
      }

      if (!comparable)
        return false;

      regex_t re;
      if (regcomp(&re, term->value.s, REG_EXTENDED | REG_NOSUB) != 0)
        return false;

      bool m = false;

      if (valueP->type == CorString)
        m = (regexec(&re, valueP->value.s, 0, NULL, 0) == 0);
      else
      {
        for (CorNode* elemP = valueP->value.head; elemP != NULL; elemP = elemP->next)
        {
          if ((elemP->type == CorString) && (regexec(&re, elemP->value.s, 0, NULL, 0) == 0))
          { m = true; break; }
        }
      }

      regfree(&re);

      return (term->op == LdQPattern) ? m : !m;
    }

    // Equality / ordering. Scalar path identical to before; array path
    // checks "any element matches" for ==, "no element matches" for !=
    // (the spec's array-containment semantics, mirroring BSON $eq/$ne).
    if (valueP->type == CorArray)
    {
      bool hit = false;
      for (CorNode* elemP = valueP->value.head; elemP != NULL; elemP = elemP->next)
      {
        if (elemP->type == CorString && strcmp(elemP->value.s, term->value.s) == 0)
        { hit = true; break; }
      }
      switch (term->op)
      {
      case LdQEqual:   return hit;
      case LdQUnequal: return !hit;
      default:         return false;   // ordering on array doesn't have a sensible semantic
      }
    }
    if (valueP->type != CorString) return (term->op == LdQUnequal);
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
    if (valueP->type == CorArray)
    {
      bool hit = false;
      for (CorNode* elemP = valueP->value.head; elemP != NULL; elemP = elemP->next)
      {
        if (elemP->type == CorBoolean && elemP->value.b == term->value.b) { hit = true; break; }
      }
      switch (term->op)
      {
      case LdQEqual:   return hit;
      case LdQUnequal: return !hit;
      default:         return false;
      }
    }
    if (valueP->type != CorBoolean) return (term->op == LdQUnequal);
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
    if (valueP->type != CorInt) return (term->op == LdQUnequal);
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
    if (term->op == LdQPattern && valueP->type == CorString)
    {
      regex_t re;
      if (regcomp(&re, term->value.s, REG_EXTENDED | REG_NOSUB) == 0)
      {
        bool m = (regexec(&re, valueP->value.s, 0, NULL, 0) == 0);
        regfree(&re);
        return m;
      }
    }
    else if (term->op == LdQNotPattern && valueP->type == CorString)
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
    //
    // An array target: any element inside the interval satisfies it, the way
    // any element equal to the value satisfies == (§ 7.2.3.4 condition 1). The
    // spec spells the array case out for a single value and for a value list
    // (condition 2) and says nothing for a range - but a Property whose value
    // is an array does not stop being one because the query is a range, and
    // mongoc's $gte/$lte compare element-wise natively. See spec-doubts-2 #123.
    //
    if (valueP->type == CorArray)
    {
      bool hit = false;
      for (CorNode* elemP = valueP->value.head; elemP != NULL; elemP = elemP->next)
      {
        double elemNum;
        if      (elemP->type == CorInt)  elemNum = (double) elemP->value.i;
        else if (elemP->type == CorFloat) elemNum = elemP->value.f;
        else continue;

        if ((elemNum >= term->value.numRange.lo) && (elemNum <= term->value.numRange.hi))
        { hit = true; break; }
      }

      switch (term->op)
      {
      case LdQEqual:   return hit;
      case LdQUnequal: return !hit;
      default:         return false;
      }
    }

    if (!isNum) return (term->op == LdQUnequal);
    if (term->op == LdQEqual)
      return entityNum >= term->value.numRange.lo && entityNum <= term->value.numRange.hi;
    else if (term->op == LdQUnequal)
      return entityNum < term->value.numRange.lo || entityNum > term->value.numRange.hi;
    return false;

  case LdQValueList:
    //
    // Per ITEM, not per list: § 7.2.3.4 condition 2 asks whether the target is
    // "identical or equivalent to ANY of the list values", and says nothing
    // about the values sharing a type - `q=a==1,"two"` is a legal list. The
    // types come from the parser, which converted and validated each item; the
    // strtod here is on a token already proven to be a whole number.
    //
    //
    // An array target, § 7.2.3.4 condition 2 second bullet: "the target value
    // includes any of the Query Term values, and the target value is an array".
    // mongoc gets this from BSON's $in; the walk has to do it by hand.
    //
    if (valueP->type == CorArray)
    {
      bool hit = false;

      for (int i = 0; (i < term->value.list.count) && !hit; i++)
      {
        LdQValueType itemType = term->value.list.itemTypeV[i];

        for (CorNode* elemP = valueP->value.head; elemP != NULL; elemP = elemP->next)
        {
          if ((itemType == LdQNumber) && ((elemP->type == CorInt) || (elemP->type == CorFloat)))
          {
            double elemNum = (elemP->type == CorInt) ? (double) elemP->value.i : elemP->value.f;
            if (elemNum == strtod(term->value.list.values[i], NULL)) { hit = true; break; }
          }
          else if ((itemType == LdQString) && (elemP->type == CorString))
          {
            if (strcmp(elemP->value.s, term->value.list.values[i]) == 0) { hit = true; break; }
          }
          else if ((itemType == LdQBool) && (elemP->type == CorBoolean))
          {
            if (elemP->value.b == (strcmp(term->value.list.values[i], "true") == 0)) { hit = true; break; }
          }
        }
      }

      return (term->op == LdQEqual) ? hit : !hit;
    }

    for (int i = 0; i < term->value.list.count; i++)
    {
      LdQValueType itemType = term->value.list.itemTypeV[i];

      if ((itemType == LdQNumber) && isNum)
      {
        if (entityNum == strtod(term->value.list.values[i], NULL))
          return (term->op == LdQEqual);
      }
      else if ((itemType == LdQString) && (valueP->type == CorString))
      {
        if (strcmp(valueP->value.s, term->value.list.values[i]) == 0)
          return (term->op == LdQEqual);
      }
      else if ((itemType == LdQBool) && (valueP->type == CorBoolean))
      {
        if (valueP->value.b == (strcmp(term->value.list.values[i], "true") == 0))
          return (term->op == LdQEqual);
      }
      else if ((itemType == LdQDateTime) && (valueP->type == CorInt))
      {
        if ((long long) valueP->value.i == (long long) ldIsoToNanoseconds(term->value.list.values[i]))
          return (term->op == LdQEqual);
      }
    }

    // Nothing matched. Per § 7.2.3.3 that is "unequal" - including when the
    // target's type differs from every item's, which is a mismatch and so
    // "considered unequal" too.
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
static const char* findRelationshipTargetId(CorNode* entityP, const char* relName)
{
  if (entityP == NULL || relName == NULL || entityP->type != CorObject)
    return NULL;

  for (CorNode* attrP = entityP->value.head; attrP != NULL; attrP = attrP->next)
  {
    if (attrP->name == NULL || attrP->type != CorObject)
      continue;
    if (strcmp(attrP->name, relName) != 0)
      continue;

    for (CorNode* instP = attrP->value.head; instP != NULL; instP = instP->next)
    {
      if (instP->type != CorObject)
        continue;

      if (ldAttrTypeDetect(instP) != LdAttrRelationship)   // the member, or the node's kind
        continue;

      CorNode* valP = corTreeLookup(instP, "value");
      if (valP != NULL && valP->type == CorString)
        return valP->value.s;
    }
  }
  return NULL;
}



//
// ldEntityMatchQEx -
//
bool ldEntityMatchQEx(CorNode* entityP, LdQNode* node,
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

    CorNode* targetP = NULL;
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



bool ldEntityMatchQ(CorNode* entityP, LdQNode* node)
{
  return ldEntityMatchQEx(entityP, node, NULL, NULL);
}
