//
// FILE            ldOrderSort.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Sort a KjArray of entities in-place per NGSI-LD orderBy terms (§ 4.23).
// Entities are in storage format: each attr is a dataset-keyed wrapper
// { "@none": { type, value, ... } }.
//
#include <stdlib.h>                                    // qsort
#include <string.h>                                    // strcmp

#include "kjson/KjNode.h"                              // KjNode, KjObject, KjArray
#include "kjson/kjLookup.h"                            // kjLookup

#include "swNgsild/LdOrder.h"                          // LdOrderTerm, LdOrderDir
#include "swNgsild/ldOrderSort.h"                      // Own interface



// Thread-local context for qsort comparator (no qsort_r on all platforms)
static __thread LdOrderTerm* sortTerms;
static __thread int          sortTermCount;



// -----------------------------------------------------------------------------
//
// valueRank - type-based sort rank per § 4.23.2
//
// Numbers < Strings < Object < Array < Boolean < Null < Missing
//
static int valueRank(KjNode* valP)
{
  if (valP == NULL)         return 99;

  switch (valP->type)
  {
    case KjInt:
    case KjFloat:           return 0;
    case KjString:          return 1;
    case KjObject:          return 2;
    case KjArray:           return 3;
    case KjBoolean:         return 4;
    case KjNull:            return 5;
    default:                return 6;
  }
}



// -----------------------------------------------------------------------------
//
// getAttrValue - extract a representative "value" for ordering purposes
//
// Two shapes are supported:
//
//   - Current-state (storage): attrWrapper → "@none" → { type, value }
//   - Temporal API (§ 5.7.3):  attrArray → [ { type, value, observedAt, ... }, ... ]
//
// For the temporal shape we walk the array and pick the instance with the
// largest observedAt (falling back to modifiedAt, then to the last array
// element). That gives users an intuitive "most recent value" for orderBy
// rather than the SQL-ordered first/last entry, which would flip with lastN.
//
static KjNode* temporalLatestValue(KjNode* arrayP)
{
  KjNode* bestP = NULL;
  const char* bestKey = NULL;

  for (KjNode* instP = arrayP->value.firstChildP; instP != NULL; instP = instP->next)
  {
    if (instP->type != KjObject)
      continue;

    KjNode* obsP = kjLookup(instP, "observedAt");
    if (obsP == NULL) obsP = kjLookup(instP, "modifiedAt");
    const char* key = (obsP != NULL && obsP->type == KjString) ? obsP->value.s : "";

    if (bestP == NULL || (bestKey != NULL && strcmp(key, bestKey) > 0))
    {
      bestP   = instP;
      bestKey = key;
    }
  }

  return (bestP != NULL) ? kjLookup(bestP, "value") : NULL;
}


static KjNode* getAttrValue(KjNode* entityP, const char* attrName)
{
  KjNode* wrapperP = kjLookup(entityP, attrName);
  if (wrapperP == NULL)
    return NULL;

  if (wrapperP->type == KjArray)
    return temporalLatestValue(wrapperP);

  if (wrapperP->type != KjObject)
    return NULL;

  KjNode* noneP = kjLookup(wrapperP, "@none");
  if (noneP == NULL || noneP->type != KjObject)
    return NULL;

  return kjLookup(noneP, "value");
}



// -----------------------------------------------------------------------------
//
// compareValues - compare two attr values for sorting
//
static int compareValues(KjNode* a, KjNode* b)
{
  int ra = valueRank(a);
  int rb = valueRank(b);
  if (ra != rb)
    return ra - rb;

  if (a == NULL || b == NULL)
    return 0;

  switch (a->type)
  {
    case KjInt:
      return (a->value.i < b->value.i) ? -1 : (a->value.i > b->value.i) ? 1 : 0;

    case KjFloat:
      return (a->value.f < b->value.f) ? -1 : (a->value.f > b->value.f) ? 1 : 0;

    case KjString:
      return strcmp(a->value.s, b->value.s);

    case KjBoolean:
      return (int)(a->value.b) - (int)(b->value.b);

    default:
      return 0;
  }
}



// -----------------------------------------------------------------------------
//
// entityCompare - qsort comparator for entity pointers
//
static int entityCompare(const void* pa, const void* pb)
{
  KjNode* a = *(KjNode**) pa;
  KjNode* b = *(KjNode**) pb;

  for (int i = 0; i < sortTermCount; i++)
  {
    KjNode* va = getAttrValue(a, sortTerms[i].attrName);
    KjNode* vb = getAttrValue(b, sortTerms[i].attrName);

    int cmp = compareValues(va, vb);
    if (cmp != 0)
      return (sortTerms[i].dir == LdOrderDesc) ? -cmp : cmp;
  }

  return 0;
}



// -----------------------------------------------------------------------------
//
// ldOrderSort - sort an entity array in-place
//
void ldOrderSort(KjNode* arrayP, LdOrderTerm* terms, int termCount)
{
  if (arrayP == NULL || arrayP->type != KjArray || terms == NULL || termCount == 0)
    return;

  // Count entities
  int count = 0;
  for (KjNode* p = arrayP->value.firstChildP; p != NULL; p = p->next)
    count++;

  if (count < 2)
    return;

  // Build pointer array for qsort
  KjNode** ptrV = (KjNode**) malloc(count * sizeof(KjNode*));
  int ix = 0;
  for (KjNode* p = arrayP->value.firstChildP; p != NULL; p = p->next)
    ptrV[ix++] = p;

  // Set thread-local sort context
  sortTerms     = terms;
  sortTermCount = termCount;

  qsort(ptrV, count, sizeof(KjNode*), entityCompare);

  // Re-link the list in sorted order
  arrayP->value.firstChildP = ptrV[0];
  for (int i = 0; i < count - 1; i++)
    ptrV[i]->next = ptrV[i + 1];
  ptrV[count - 1]->next = NULL;
  arrayP->lastChild = ptrV[count - 1];

  free(ptrV);
}
