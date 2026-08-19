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
#include <string.h>                                    // strcmp, strcasecmp

#ifdef COR_WITH_ICU
#include <unicode/ucol.h>                              // UCollator, ucol_open, ucol_strcollUTF8
#endif

#include "kjson/KjNode.h"                              // KjNode, KjObject, KjArray
#include "kjson/kjLookup.h"                            // kjLookup

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
static KjNode* temporalLatestInstance(KjNode* arrayP)
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

  return bestP;
}


static KjNode* temporalLatestValue(KjNode* arrayP)
{
  KjNode* bestP = temporalLatestInstance(arrayP);
  return (bestP != NULL) ? kjLookup(bestP, "value") : NULL;
}


// lookupSeg - linear lookup of a child by exact name in a KjObject.
static KjNode* lookupSeg(KjNode* base, const char* seg)
{
  if (base == NULL || base->type != KjObject || seg == NULL)
    return NULL;
  for (KjNode* c = base->value.firstChildP; c != NULL; c = c->next)
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
static KjNode* getAttrValueByPath(KjNode* entityP, char** segV, int segN)
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
    return kjLookup(entityP, first);
  }

  KjNode* wrapperP = kjLookup(entityP, first);
  if (wrapperP == NULL)
    return NULL;

  if (wrapperP->type == KjArray)            // temporal store
  {
    if (segN == 1)
      return temporalLatestValue(wrapperP);

    // segN > 1: path-into-temporal — drill into the most-recent instance
    // and resolve segments[1..] inside it (e.g. orderBy=name.createdAt
    // sorts by the createdAt of the latest `name` instance).
    KjNode* instP = temporalLatestInstance(wrapperP);
    if (instP == NULL)
      return NULL;

    KjNode* cur = instP;
    for (int i = 1; i < segN; i++)
    {
      cur = kjLookup(cur, segV[i]);
      if (cur == NULL) return NULL;
    }
    if (cur != NULL && cur->type == KjObject)
    {
      KjNode* v = kjLookup(cur, "value");
      if (v != NULL) return v;
    }
    return cur;
  }
  if (wrapperP->type != KjObject)
    return NULL;

  KjNode* noneP = kjLookup(wrapperP, "@none");
  if (noneP == NULL || noneP->type != KjObject)
    return NULL;

  if (segN == 1)
    return kjLookup(noneP, "value");

  // Walk remaining segments inside @none.
  KjNode* cur = noneP;
  for (int i = 1; i < segN; i++)
  {
    cur = lookupSeg(cur, segV[i]);
    if (cur == NULL) return NULL;

    // If more segments follow, peel any nested @none wrapper.
    if (i + 1 < segN && cur->type == KjObject)
    {
      KjNode* none = kjLookup(cur, "@none");
      if (none != NULL && none->type == KjObject)
        cur = none;
    }
  }

  // Leaf: a Property-shaped object → return its scalar `value`.
  if (cur != NULL && cur->type == KjObject)
  {
    KjNode* v = kjLookup(cur, "value");
    if (v != NULL) return v;
  }
  return cur;
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
      return strCollate(a->value.s, b->value.s);

    case KjBoolean:
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
static KjNode* descendValuePath(KjNode* valP, char** memberV, int memberN)
{
  for (int i = 0; (i < memberN) && (valP != NULL); i++)
    valP = (valP->type == KjObject) ? kjLookup(valP, memberV[i]) : NULL;
  return valP;
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
    if (sortTerms[i].byDistance)
    {
      // § 7.6.2.2 — order by the `geoDistance` the plugin attached (in metres
      // from ?orderFrom). Entities that convey the GeoProperty (geoDistance
      // present) rank ahead of those that do not, regardless of direction; among
      // geo-bearing entities the order is ascending (nearest first) or, for
      // dist-desc, descending.
      KjNode* da = kjLookup(a, "geoDistance");
      KjNode* dbn = kjLookup(b, "geoDistance");
      bool    ha = (da  != NULL) && (da->type  == KjFloat || da->type  == KjInt);
      bool    hb = (dbn != NULL) && (dbn->type == KjFloat || dbn->type == KjInt);

      if (ha != hb)
        return ha ? -1 : 1;   // geo-bearing first
      if (!ha)
        continue;             // neither carries a distance — try the next term

      double va = (da->type  == KjFloat) ? da->value.f  : (double) da->value.i;
      double vb = (dbn->type == KjFloat) ? dbn->value.f : (double) dbn->value.i;
      if (va != vb)
      {
        int cmp = (va < vb) ? -1 : 1;
        return (sortTerms[i].dir == LdOrderDesc) ? -cmp : cmp;
      }
      continue;               // equal distance — try the next term
    }

    KjNode* va = getAttrValueByPath(a, sortTerms[i].pathSegV, sortTerms[i].pathSegN);
    KjNode* vb = getAttrValueByPath(b, sortTerms[i].pathSegV, sortTerms[i].pathSegN);

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
    bool aNil = (va == NULL) || (va->type == KjNull);
    bool bNil = (vb == NULL) || (vb->type == KjNull);
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
void ldOrderSort(KjNode* arrayP, LdOrderTerm* terms, int termCount, const char* collation)
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

  qsort(ptrV, count, sizeof(KjNode*), entityCompare);

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
