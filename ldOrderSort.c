//
// FILE            ldOrderSort.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Sort a CorArray of entities in-place per NGSI-LD orderBy terms (§ 4.23).
// Entities are in storage format: each attr is a dataset-keyed wrapper
// { "@none": { type, value, ... } }.
//
#include <stdlib.h>                                    // qsort
#include <string.h>                                    // strcmp, strcasecmp

#ifdef COR_WITH_ICU
#include <unicode/ucol.h>                              // UCollator, ucol_open, ucol_strcollUTF8
#endif

#include "corTree/CorNode.h"                           // CorNode, CorObject, CorArray
#include "corTree/corTreeLookup.h"                     // corTreeLookup

#include "corNgsild/LdOrder.h"                          // LdOrderTerm, LdOrderDir
#include "corNgsild/ldOrderSort.h"                      // Own interface



// Thread-local context for qsort comparator (no qsort_r on all platforms)
static __thread LdOrderTerm* sortTerms;
static __thread int          sortTermCount;

#ifdef COR_WITH_ICU
// Per-sort ICU collator, opened once in ldOrderSort and read by the comparator
// (opening one per comparison would be catastrophic inside qsort).
static __thread UCollator*   sortCollatorP;
#endif



// -----------------------------------------------------------------------------
//
// strCollate - compare two UTF-8 strings for orderBy string ordering (§ 7.6.2.1)
//
// § 7.6.2.1 mandates ICU "root" collation as the default (a "reasonable
// language-agnostic" order, case-insensitive at the primary level). An ICU
// build uses the opened collator (root, or the requested collation= locale);
// any other build falls back to a dependency-free approximation: compare
// case-insensitively, and for otherwise-equal strings order lowercase before
// uppercase (matching ICU root's tertiary preference), so e.g.
// apple < Apple < banana < Banana < cherry — never the raw-byte order where
// every uppercase initial sorts ahead of every lowercase one.
//
static int strCollate(const char* a, const char* b)
{
#ifdef COR_WITH_ICU
  if (sortCollatorP != NULL)
  {
    UErrorCode        ec = U_ZERO_ERROR;
    UCollationResult  r  = ucol_strcollUTF8(sortCollatorP, a, -1, b, -1, &ec);
    if (U_SUCCESS(ec))
      return (r == UCOL_LESS) ? -1 : (r == UCOL_GREATER) ? 1 : 0;
    // ec failure (e.g. malformed UTF-8) — fall through to the ASCII approximation
  }
#endif

  int c = strcasecmp(a, b);
  if (c != 0)
    return c;
  return -strcmp(a, b);   // equal ignoring case → lowercase before uppercase
}



// -----------------------------------------------------------------------------
//
// valueRank - type-based sort rank per § 4.23.2
//
// Numbers < Strings < Object < Array < Boolean < Null < Missing
//
static int valueRank(CorNode* valP)
{
  if (valP == NULL)         return 99;

  switch (valP->type)
  {
    case CorInt:
    case CorFloat:          return 0;
    case CorString:         return 1;
    case CorObject:         return 2;
    case CorArray:          return 3;
    case CorBoolean:        return 4;
    case CorNull:           return 5;
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
static CorNode* temporalLatestInstance(CorNode* arrayP)
{
  CorNode* bestP = NULL;
  const char* bestKey = NULL;

  for (CorNode* instP = arrayP->value.firstChildP; instP != NULL; instP = instP->next)
  {
    if (instP->type != CorObject)
      continue;

    CorNode* obsP = corTreeLookup(instP, "observedAt");
    if (obsP == NULL) obsP = corTreeLookup(instP, "modifiedAt");
    const char* key = (obsP != NULL && obsP->type == CorString) ? obsP->value.s : "";

    if (bestP == NULL || (bestKey != NULL && strcmp(key, bestKey) > 0))
    {
      bestP   = instP;
      bestKey = key;
    }
  }

  return bestP;
}


static CorNode* temporalLatestValue(CorNode* arrayP)
{
  CorNode* bestP = temporalLatestInstance(arrayP);
  return (bestP != NULL) ? corTreeLookup(bestP, "value") : NULL;
}


// lookupSeg - linear lookup of a child by exact name in a CorObject.
static CorNode* lookupSeg(CorNode* base, const char* seg)
{
  if (base == NULL || base->type != CorObject || seg == NULL)
    return NULL;
  for (CorNode* c = base->value.firstChildP; c != NULL; c = c->next)
    if (c->name != NULL && strcmp(c->name, seg) == 0)
      return c;
  return NULL;
}



// getAttrValueByPath - resolve an orderBy term's already-expanded segments.
// Walking by string path is unsafe — expanded IRIs contain dots themselves
// (`https://uri.etsi.org/...`). The expansion step pre-splits and stores the
// segments as a NULL-terminated array.
//
//   orderBy=id                              → segV=["id"]                    → entity[id]
//   orderBy=name                            → segV=["<iri-name>"]            → @none.value
//   orderBy=name.createdAt                  → segV=["<iri-name>", "createdAt"]
//   orderBy=name.subProperty                → segV=["<iri-name>", "<iri-sub>"] → sub.value
//
static CorNode* getAttrValueByPath(CorNode* entityP, char** segV, int segN)
{
  if (entityP == NULL || segV == NULL || segN <= 0)
    return NULL;

  const char* first = segV[0];

  // Reserved entity members are first-class fields on the entity (not
  // wrapped). Only valid as a single-segment path.
  if (segN == 1 &&
      (strcmp(first, "id")         == 0 ||
       strcmp(first, "type")       == 0 ||
       strcmp(first, "scope")      == 0 ||
       strcmp(first, "createdAt")  == 0 ||
       strcmp(first, "modifiedAt") == 0))
  {
    return corTreeLookup(entityP, first);
  }

  CorNode* wrapperP = corTreeLookup(entityP, first);
  if (wrapperP == NULL)
    return NULL;

  if (wrapperP->type == CorArray)           // temporal store
  {
    if (segN == 1)
      return temporalLatestValue(wrapperP);

    // segN > 1: path-into-temporal — drill into the most-recent instance
    // and resolve segments[1..] inside it (e.g. orderBy=name.createdAt
    // sorts by the createdAt of the latest `name` instance).
    CorNode* instP = temporalLatestInstance(wrapperP);
    if (instP == NULL)
      return NULL;

    CorNode* cur = instP;
    for (int i = 1; i < segN; i++)
    {
      cur = corTreeLookup(cur, segV[i]);
      if (cur == NULL) return NULL;
    }
    if (cur != NULL && cur->type == CorObject)
    {
      CorNode* v = corTreeLookup(cur, "value");
      if (v != NULL) return v;
    }
    return cur;
  }
  if (wrapperP->type != CorObject)
    return NULL;

  CorNode* noneP = corTreeLookup(wrapperP, "@none");
  if (noneP == NULL || noneP->type != CorObject)
    return NULL;

  if (segN == 1)
    return corTreeLookup(noneP, "value");

  // Walk remaining segments inside @none.
  CorNode* cur = noneP;
  for (int i = 1; i < segN; i++)
  {
    cur = lookupSeg(cur, segV[i]);
    if (cur == NULL) return NULL;

    // If more segments follow, peel any nested @none wrapper.
    if (i + 1 < segN && cur->type == CorObject)
    {
      CorNode* none = corTreeLookup(cur, "@none");
      if (none != NULL && none->type == CorObject)
        cur = none;
    }
  }

  // Leaf: a Property-shaped object → return its scalar `value`.
  if (cur != NULL && cur->type == CorObject)
  {
    CorNode* v = corTreeLookup(cur, "value");
    if (v != NULL) return v;
  }
  return cur;
}



// -----------------------------------------------------------------------------
//
// compareValues - compare two attr values for sorting
//
static int compareValues(CorNode* a, CorNode* b)
{
  int ra = valueRank(a);
  int rb = valueRank(b);
  if (ra != rb)
    return ra - rb;

  if (a == NULL || b == NULL)
    return 0;

  switch (a->type)
  {
    case CorInt:
      return (a->value.i < b->value.i) ? -1 : (a->value.i > b->value.i) ? 1 : 0;

    case CorFloat:
      return (a->value.f < b->value.f) ? -1 : (a->value.f > b->value.f) ? 1 : 0;

    case CorString:
      return strCollate(a->value.s, b->value.s);

    case CorBoolean:
      return (int)(a->value.b) - (int)(b->value.b);

    default:
      return 0;
  }
}



// -----------------------------------------------------------------------------
//
// descendValuePath - § 7.6.2.3 trailing path
//
// Descend a compound Property value (raw JSON object) by a list of member
// names (from orderBy=attr[a.b]). An undefined member yields NULL — the target
// is "non-existent" (§ 7.6.2.3), so the entity sorts last via the null-last
// rule in entityCompare.
//
static CorNode* descendValuePath(CorNode* valP, char** memberV, int memberN)
{
  for (int i = 0; (i < memberN) && (valP != NULL); i++)
    valP = (valP->type == CorObject) ? corTreeLookup(valP, memberV[i]) : NULL;
  return valP;
}



// -----------------------------------------------------------------------------
//
// entityCompare - qsort comparator for entity pointers
//
static int entityCompare(const void* pa, const void* pb)
{
  CorNode* a = *(CorNode**) pa;
  CorNode* b = *(CorNode**) pb;

  for (int i = 0; i < sortTermCount; i++)
  {
    if (sortTerms[i].byDistance)
    {
      // § 7.6.2.2 — order by the `geoDistance` the plugin attached (in metres
      // from ?orderFrom). Entities that convey the GeoProperty (geoDistance
      // present) rank ahead of those that do not, regardless of direction; among
      // geo-bearing entities the order is ascending (nearest first) or, for
      // dist-desc, descending.
      CorNode* da = corTreeLookup(a, "geoDistance");
      CorNode* dbn = corTreeLookup(b, "geoDistance");
      bool    ha = (da  != NULL) && (da->type  == CorFloat || da->type == CorInt);
      bool    hb = (dbn != NULL) && (dbn->type == CorFloat || dbn->type == CorInt);

      if (ha != hb)
        return ha ? -1 : 1;   // geo-bearing first
      if (!ha)
        continue;             // neither carries a distance — try the next term

      double va = (da->type  == CorFloat) ? da->value.f : (double) da->value.i;
      double vb = (dbn->type == CorFloat) ? dbn->value.f : (double) dbn->value.i;
      if (va != vb)
      {
        int cmp = (va < vb) ? -1 : 1;
        return (sortTerms[i].dir == LdOrderDesc) ? -cmp : cmp;
      }
      continue;               // equal distance — try the next term
    }

    CorNode* va = getAttrValueByPath(a, sortTerms[i].pathSegV, sortTerms[i].pathSegN);
    CorNode* vb = getAttrValueByPath(b, sortTerms[i].pathSegV, sortTerms[i].pathSegN);

    // § 7.6.2.3 trailing path: descend into the attribute's compound JSON value.
    if (sortTerms[i].valuePathN > 0)
    {
      va = descendValuePath(va, sortTerms[i].valuePathV, sortTerms[i].valuePathN);
      vb = descendValuePath(vb, sortTerms[i].valuePathV, sortTerms[i].valuePathN);
    }

    // § 7.6.2.2 "(null values last)": an Attribute whose value is Null, or that
    // does not exist, shall always sort LAST — irrespective of the asc/desc
    // direction. Handle it before the directional negation below, which would
    // otherwise flip a null/missing entity to the front on ;desc.
    bool aNil = (va == NULL) || (va->type == CorNull);
    bool bNil = (vb == NULL) || (vb->type == CorNull);
    if (aNil != bNil)
      return aNil ? 1 : -1;
    if (aNil)
      continue;                 // both null/missing — equal for this term

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
void ldOrderSort(CorNode* arrayP, LdOrderTerm* terms, int termCount, const char* collation)
{
  if (arrayP == NULL || arrayP->type != CorArray || terms == NULL || termCount == 0)
    return;

  // Count entities
  int count = 0;
  for (CorNode* p = arrayP->value.firstChildP; p != NULL; p = p->next)
    count++;

  if (count < 2)
    return;

  // Build pointer array for qsort
  CorNode** ptrV = (CorNode**) malloc(count * sizeof(CorNode*));
  int ix = 0;
  for (CorNode* p = arrayP->value.firstChildP; p != NULL; p = p->next)
    ptrV[ix++] = p;

  // Set thread-local sort context
  sortTerms     = terms;
  sortTermCount = termCount;

#ifdef COR_WITH_ICU
  // Open one collator for the whole sort: the requested collation= locale, or
  // "" for the § 7.6.2.1 root order. ICU falls back to root for an unknown tag
  // (best-effort), so an unsupported collation is never an error. On failure
  // sortCollatorP stays NULL and strCollate uses the ASCII approximation.
  UErrorCode ec = U_ZERO_ERROR;
  sortCollatorP = ucol_open((collation != NULL) ? collation : "", &ec);
  if (U_FAILURE(ec))
    sortCollatorP = NULL;
#else
  (void) collation;
#endif

  qsort(ptrV, count, sizeof(CorNode*), entityCompare);

#ifdef COR_WITH_ICU
  if (sortCollatorP != NULL)
  {
    ucol_close(sortCollatorP);
    sortCollatorP = NULL;
  }
#endif

  // Re-link the list in sorted order
  arrayP->value.firstChildP = ptrV[0];
  for (int i = 0; i < count - 1; i++)
    ptrV[i]->next = ptrV[i + 1];
  ptrV[count - 1]->next = NULL;
  arrayP->lastChild = ptrV[count - 1];

  free(ptrV);
}
