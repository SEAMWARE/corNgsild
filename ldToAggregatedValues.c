//
// FILE            ldToAggregatedValues.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// § 4.5.20 — aggregated temporal representation. See header for the shape.
//
// Numeric-Property-only first cut: the spec also defines aggregations for
// Boolean (totalCount/distinctCount only) and string (totalCount/distinct-
// Count) — those land later. Non-numeric attrs are left as-is.
//
#define _XOPEN_SOURCE 700                              // strptime
#define _DEFAULT_SOURCE                                // timegm
#include <math.h>                                        // sqrt
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // uint64_t
#include <stdio.h>                                       // snprintf
#include <stdlib.h>                                      // strtoll / strtod
#include <string.h>                                      // strcmp, strcpy, strchr, memset
#include <time.h>                                        // strptime, timegm, gmtime_r

#include "kalloc/KAlloc.h"                              // KAlloc
#include "kalloc/kaAlloc.h"                             // kaAlloc
#include "kjson/kjson.h"                                // Kjson
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjBuilder.h"                            // kjObject, kjArray, kjString, kjFloat, kjChildAdd
#include "kjson/kjLookup.h"                             // kjLookup
#include "kjson/kjRender.h"                             // kjFastRender
#include "kjson/kjRenderSize.h"                         // kjFastRenderSize

#include "swNgsild/ldIsEntityKeyword.h"                  // ldIsEntityKeyword
#include "swNgsild/ldToAggregatedValues.h"               // Own interface



// -----------------------------------------------------------------------------
//
// ldIso8601DurationToNs - parse PnYnMnDTnHnMnS into nanoseconds.
//
// Years and months are intentionally rejected — calendar lengths aren't
// fixed, and the temporal API's bucketing math relies on a constant period.
//
uint64_t ldIso8601DurationToNs(const char* iso)
{
  if (iso == NULL || *iso != 'P')
    return 0;

  iso++;
  bool inTime = false;
  uint64_t totalNs = 0;

  while (*iso != 0)
  {
    if (*iso == 'T')
    {
      inTime = true;
      iso++;
      continue;
    }

    char* end = NULL;
    long long n = strtoll(iso, &end, 10);
    if (end == iso || n < 0)
      return 0;

    char unit = *end;
    iso = end + 1;

    uint64_t mult = 0;
    if (!inTime)
    {
      if (unit == 'D')                     mult = 86400ULL * 1000000000ULL;
      else                                 return 0;            // Y / M / W not supported
    }
    else
    {
      if      (unit == 'H')                mult = 3600ULL * 1000000000ULL;
      else if (unit == 'M')                mult = 60ULL   * 1000000000ULL;
      else if (unit == 'S')                mult = 1000000000ULL;
      else                                 return 0;
    }

    totalNs += (uint64_t) n * mult;
  }

  return totalNs;
}



// -----------------------------------------------------------------------------
//
// isoToNs - parse an ISO 8601 datetime into epoch ns (best-effort).
//
static uint64_t isoToNs(const char* iso)
{
  if (iso == NULL)
    return 0;

  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  const char* rest = strptime(iso, "%Y-%m-%dT%H:%M:%S", &tm);
  if (rest == NULL)
    return 0;

  uint64_t ns = (uint64_t) timegm(&tm) * 1000000000ULL;

  if (*rest == '.')
  {
    rest++;
    long frac = 0; int digits = 0;
    while (*rest >= '0' && *rest <= '9' && digits < 9)
    {
      frac = frac * 10 + (*rest - '0');
      rest++;
      digits++;
    }
    while (digits < 9) { frac *= 10; digits++; }
    ns += (uint64_t) frac;
  }

  return ns;
}



// -----------------------------------------------------------------------------
//
// nsToIso - format an epoch-ns timestamp as ISO 8601 (UTC). Emits the
// `.SSS` fractional second only when the sub-second component is non-zero,
// so a clean second renders as `2020-08-01T12:03:00Z` (matching the
// canonical fixtures used by ETSI's aggregated-representation tests).
//
static char* nsToIso(uint64_t ns, KAlloc* faP)
{
  const int sz = 64;
  char* buf = (char*) kaAlloc(faP, sz);
  time_t t  = (time_t) (ns / 1000000000ULL);
  long   ms = (long) ((ns % 1000000000ULL) / 1000000);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  if (ms == 0)
  {
    snprintf(buf, sz, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  }
  else
  {
    snprintf(buf, sz, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
  }
  return buf;
}



// -----------------------------------------------------------------------------
//
// Bucket - per-period accumulator.
//
// Per § 4.5.19.1 (tables 1 and 3) each method is valid only for certain
// value types. The bucket carries three parallel sub-accumulators so that
// methods can read whichever is appropriate:
//
//   - numeric:  Number, Boolean (true=1/false=0, per the table's note),
//               and Array (stores the array SIZE — "max array size in the
//               period", etc., per the JSON-Array column of table 1).
//   - string:   String values; tracks lexicographic min/max.
//   - object:   Compound JSON Object Property values; only contributes
//               to totalCount + distinctCount per the spec.
//
// distinctCount is computed across all kinds via stringified values
// (numbers → %.17g, bools → "true"/"false", strings verbatim, arrays/
// objects → compact JSON via kjFastRender).
//
// Relationships (table 3) only allow totalCount / distinctCount and are
// fed into the string accumulator using the relationship's `object` URI
// — emitValueArray gates by attrType so the lex methods aren't exposed.
//
#define BUCKET_DISTINCT_CAP  64

typedef struct Bucket
{
  uint64_t  startNs;

  // Numeric sub-accumulator (Number / Boolean-as-0-1 / Array-size).
  int       numericCount;
  double    sum;
  double    sumsq;
  double    minV;
  double    maxV;

  // String sub-accumulator (Property String values + Relationship object).
  int       stringCount;
  const char* lexMin;
  const char* lexMax;

  // Compound JSON Object Property values — only count, no per-value math.
  int       objectCount;

  // Distinct accumulator across all kinds — pointers into the per-request
  // alloc arena, capped at BUCKET_DISTINCT_CAP.
  const char* distinctV[BUCKET_DISTINCT_CAP];
  int         distinctN;
} Bucket;



static void bucketInit(Bucket* b, uint64_t startNs)
{
  memset(b, 0, sizeof(*b));
  b->startNs = startNs;
}



static int bucketTotalCount(const Bucket* b)
{
  return b->numericCount + b->stringCount + b->objectCount;
}



// Add a stringified canonical value to the distinct set (capped — values
// past the cap are simply not counted; keeps the bucket fixed-size).
// `s` must have lifetime ≥ this request (caller allocates from the arena).
static void bucketTrackDistinct(Bucket* b, const char* s)
{
  if (b->distinctN >= BUCKET_DISTINCT_CAP)
    return;

  for (int i = 0; i < b->distinctN; i++)
    if (strcmp(b->distinctV[i], s) == 0)
      return;

  b->distinctV[b->distinctN++] = s;
}



static void bucketAddNumber(Bucket* b, double v, KAlloc* faP)
{
  if (b->numericCount == 0)
  {
    b->minV = v;
    b->maxV = v;
  }
  else
  {
    if (v < b->minV) b->minV = v;
    if (v > b->maxV) b->maxV = v;
  }
  b->sum   += v;
  b->sumsq += v * v;
  b->numericCount++;

  char* buf = (char*) kaAlloc(faP, 32);
  snprintf(buf, 32, "%.17g", v);
  bucketTrackDistinct(b, buf);
}



// Per § 4.5.19.1 note: "true is considered as a value of 1, false as 0".
static void bucketAddBool(Bucket* b, bool v, KAlloc* faP)
{
  bucketAddNumber(b, v ? 1.0 : 0.0, faP);
}



static void bucketAddString(Bucket* b, const char* s)
{
  b->stringCount++;

  if (b->lexMin == NULL || strcmp(s, b->lexMin) < 0) b->lexMin = s;
  if (b->lexMax == NULL || strcmp(s, b->lexMax) > 0) b->lexMax = s;

  bucketTrackDistinct(b, s);
}



// Render a JSON node (compact) into a fresh kalloc buffer. Used for arrays
// and objects to feed distinctCount.
static const char* renderNodeJson(KjNode* nP, KAlloc* faP)
{
  // kjFastRender's contract for KjArray/KjObject is "open with [ or {, walk
  // children, close" — the parent name is NOT emitted, exactly what we want.
  int   sz  = kjFastRenderSize(nP) + 1;
  char* buf = (char*) kaAlloc(faP, sz);
  kjFastRender(nP, buf);
  return buf;
}



// Array: feed the size into the numeric sub-accumulator (per table 1's
// JSON-Array column: avg/sum/min/max are all "of the sizes"); feed the
// canonical JSON into distinctCount.
static void bucketAddArray(Bucket* b, KjNode* arrayP, KAlloc* faP)
{
  int sz = 0;
  for (KjNode* p = arrayP->value.firstChildP; p != NULL; p = p->next)
    sz++;

  // Replicate the numeric accumulator update without growing distinct
  // (we add the canonical-JSON form to distinct below).
  double v = (double) sz;
  if (b->numericCount == 0) { b->minV = v; b->maxV = v; }
  else { if (v < b->minV) b->minV = v; if (v > b->maxV) b->maxV = v; }
  b->sum   += v;
  b->sumsq += v * v;
  b->numericCount++;

  bucketTrackDistinct(b, renderNodeJson(arrayP, faP));
}



// Object: only totalCount and distinctCount per table 1.
static void bucketAddObject(Bucket* b, KjNode* objP, KAlloc* faP)
{
  b->objectCount++;
  bucketTrackDistinct(b, renderNodeJson(objP, faP));
}



static double bucketStddev(const Bucket* b)
{
  if (b->numericCount < 2) return 0.0;
  double mean = b->sum / b->numericCount;
  double var  = (b->sumsq / b->numericCount) - (mean * mean);
  return (var > 0.0) ? sqrt(var) : 0.0;
}



// -----------------------------------------------------------------------------
//
// emitValueArray - build [[v, ts-start, ts-end], ...] for one method.
//
static KjNode* emitValueArray(const char*   method,
                              Bucket*       buckets,
                              int           bucketCount,
                              uint64_t      periodNs,
                              const char*   attrType,
                              Kjson*        kjsonP,
                              KAlloc*       faP)
{
  KjNode* arr = kjArray(kjsonP, method);

  // § 4.5.19.1 table 3: Relationships only support totalCount and
  // distinctCount; everything else is N/A.
  bool isRelationship = (strcmp(attrType, "Relationship") == 0);

  for (int i = 0; i < bucketCount; i++)
  {
    Bucket* b = &buckets[i];
    int totalCount = bucketTotalCount(b);
    if (totalCount == 0)
      continue;

    // tupleNumeric carries the row's first element for tuples whose value
    // is a number (every method except min/max-on-strings). tupleString
    // is set when the row's first element is a lex result.
    double tupleNumeric = 0.0;
    const char* tupleString = NULL;

    bool isCount      = (strcmp(method, "totalCount")    == 0 ||
                         strcmp(method, "count")         == 0 ||
                         strcmp(method, "distinctCount") == 0);

    if (isRelationship && !isCount)
      continue;

    if      (strcmp(method, "totalCount")    == 0)  tupleNumeric = (double) totalCount;
    else if (strcmp(method, "count")         == 0)  tupleNumeric = (double) totalCount;
    else if (strcmp(method, "distinctCount") == 0)  tupleNumeric = (double) b->distinctN;
    else if (strcmp(method, "avg")           == 0)
    {
      if (b->numericCount == 0) continue;
      tupleNumeric = b->sum / b->numericCount;
    }
    else if (strcmp(method, "sum")           == 0)
    {
      if (b->numericCount == 0) continue;
      tupleNumeric = b->sum;
    }
    else if (strcmp(method, "stddev")        == 0)
    {
      if (b->numericCount < 2) continue;
      tupleNumeric = bucketStddev(b);
    }
    else if (strcmp(method, "sumsq")         == 0)
    {
      if (b->numericCount == 0) continue;
      tupleNumeric = b->sumsq;
    }
    else if (strcmp(method, "min") == 0 || strcmp(method, "max") == 0)
    {
      // Number/Bool/Array all feed numericMin/Max; pure-string buckets
      // use lex min/max. Spec doesn't define mixed buckets — when both
      // exist, the numeric answer wins (it covers the broader range of
      // common workloads).
      bool wantMin = (method[1] == 'i');
      if (b->numericCount > 0)
        tupleNumeric = wantMin ? b->minV : b->maxV;
      else if (b->stringCount > 0)
        tupleString = wantMin ? b->lexMin : b->lexMax;
      else
        continue;
    }
    else
    {
      continue;
    }

    KjNode* tuple = kjArray(kjsonP, NULL);
    if (tupleString != NULL)
      kjChildAdd(tuple, kjString(kjsonP, NULL, tupleString));
    else
      kjChildAdd(tuple, kjFloat(kjsonP, NULL, tupleNumeric));
    kjChildAdd(tuple, kjString(kjsonP, NULL, nsToIso(b->startNs, faP)));
    kjChildAdd(tuple, kjString(kjsonP, NULL, nsToIso(b->startNs + periodNs, faP)));
    kjChildAdd(arr, tuple);
  }

  return arr;
}



// -----------------------------------------------------------------------------
//
// aggregateAttr - replace a Property attr's instance array with the
//                 simplified-aggregated shape:
//                   { type, "<method>": [[v, t-start, t-end], ...], ... }
//
static void aggregateAttr(KjNode*      attrP,
                          char**       methodsV,
                          uint64_t     periodNs,
                          uint64_t     startNs,
                          uint64_t     endNs,
                          const char*  timeProp,
                          Kjson*       kjsonP,
                          KAlloc*      faP)
{
  if (attrP == NULL || attrP->type != KjArray)
    return;

  // First pass: figure out attr type + scan instances.
  KjNode* firstP = attrP->value.firstChildP;
  if (firstP == NULL || firstP->type != KjObject)
    return;

  KjNode* typeP = kjLookup(firstP, "type");
  const char* attrType = (typeP != NULL && typeP->type == KjString) ? typeP->value.s : "Property";

  // § 4.5.19.1 covers only Property (table 1) and Relationship (table 3).
  // GeoProperty / LanguageProperty / VocabProperty / List* / Json are
  // left as-is in this slice.
  bool isProperty     = (strcmp(attrType, "Property")     == 0);
  bool isRelationship = (strcmp(attrType, "Relationship") == 0);
  if (!isProperty && !isRelationship)
    return;

  // The instance field carrying the aggregated value.
  const char* valueKey = isRelationship ? "object" : "value";

  // Establish window end if caller passed 0 → take latest sample seen.
  // Establish window start if caller passed 0 → take earliest sample seen.
  // (timerel/timeAt are optional on the single-entity GET, so startNs may
  // be unset; aggregating over the full data range is the natural default.)
  uint64_t winEnd   = endNs;
  uint64_t winStart = startNs;
  if (winEnd == 0 || winStart == 0)
  {
    uint64_t earliest = UINT64_MAX;
    uint64_t latest   = 0;
    for (KjNode* instP = firstP; instP != NULL; instP = instP->next)
    {
      if (instP->type != KjObject) continue;
      KjNode* tsP = kjLookup(instP, timeProp);
      if (tsP == NULL || tsP->type != KjString) continue;
      uint64_t ts = isoToNs(tsP->value.s);
      if (ts > latest)   latest   = ts;
      if (ts < earliest) earliest = ts;
    }
    // Bump winEnd by 1 ns when derived from latest sample so the bucket
    // window includes that sample (the per-instance gate below uses a
    // half-open [start,end) check — without the bump, an attr whose
    // latest instance lands exactly on the implicit end would lose a
    // bucket and be off-by-one vs. § 4.5.19.1).
    if (winEnd   == 0) winEnd   = latest + 1;
    if (winStart == 0) winStart = earliest == UINT64_MAX ? 0 : earliest;
  }
  if (winEnd <= winStart || periodNs == 0)
    return;

  int bucketCount = (int) ((winEnd - winStart + periodNs - 1) / periodNs);
  if (bucketCount <= 0) bucketCount = 1;

  Bucket* buckets = (Bucket*) kaAlloc(faP, sizeof(Bucket) * bucketCount);
  for (int i = 0; i < bucketCount; i++)
    bucketInit(&buckets[i], winStart + (uint64_t) i * periodNs);

  // Bucket the instances. Per § 4.5.19.1:
  //   Property:     Number / Boolean / String / Array / Object — different
  //                 sub-accumulators per kind (the bucket holds all three).
  //   Relationship: object URI → string sub-accumulator only.
  for (KjNode* instP = firstP; instP != NULL; instP = instP->next)
  {
    if (instP->type != KjObject) continue;
    KjNode* valP = kjLookup(instP, valueKey);
    if (valP == NULL) continue;

    KjNode* tsP = kjLookup(instP, timeProp);
    if (tsP == NULL || tsP->type != KjString) continue;
    uint64_t ts = isoToNs(tsP->value.s);
    if (ts < winStart || ts >= winEnd) continue;

    int idx = (int) ((ts - winStart) / periodNs);
    if (idx < 0 || idx >= bucketCount) continue;
    Bucket* b = &buckets[idx];

    if (isRelationship)
    {
      if (valP->type == KjString) bucketAddString(b, valP->value.s);
      continue;
    }

    switch (valP->type)
    {
      case KjInt:     bucketAddNumber(b, (double) valP->value.i, faP); break;
      case KjFloat:   bucketAddNumber(b, valP->value.f, faP);          break;
      case KjBoolean: bucketAddBool  (b, valP->value.b != 0, faP);     break;
      case KjString:  bucketAddString(b, valP->value.s);               break;
      case KjArray:   bucketAddArray (b, valP, faP);                   break;
      case KjObject:  bucketAddObject(b, valP, faP);                   break;
      default: break;
    }
  }

  // Replace the array contents with { type, "<method>": [...], ... }.
  attrP->type              = KjObject;
  attrP->value.firstChildP = NULL;
  attrP->lastChild         = NULL;
  kjChildAdd(attrP, kjString(kjsonP, "type", attrType));

  for (int m = 0; methodsV != NULL && methodsV[m] != NULL; m++)
  {
    KjNode* methodArr = emitValueArray(methodsV[m], buckets, bucketCount,
                                       periodNs, attrType, kjsonP, faP);
    kjChildAdd(attrP, methodArr);
  }
}



// -----------------------------------------------------------------------------
//
// aggregateEntity - apply aggregateAttr to every Property and Relationship
//                   attribute of one entity.
//
static void aggregateEntity(KjNode*       entityP,
                            char**        methodsV,
                            uint64_t      periodNs,
                            uint64_t      startNs,
                            uint64_t      endNs,
                            const char*   timeProp,
                            Kjson*        kjsonP,
                            KAlloc*       faP)
{
  if (entityP == NULL || entityP->type != KjObject)
    return;

  for (KjNode* childP = entityP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (childP->name == NULL || ldIsEntityKeyword(childP->name))
      continue;
    if (childP->type != KjArray)
      continue;
    aggregateAttr(childP, methodsV, periodNs, startNs, endNs, timeProp, kjsonP, faP);
  }
}



// -----------------------------------------------------------------------------
//
// ldToAggregatedValues -
//
void ldToAggregatedValues(KjNode*       treeP,
                          char**        methodsV,
                          uint64_t      periodNs,
                          uint64_t      startNs,
                          uint64_t      endNs,
                          const char*   timeProp,
                          Kjson*        kjsonP,
                          KAlloc*       faP)
{
  if (treeP == NULL || methodsV == NULL || methodsV[0] == NULL)
    return;
  if (timeProp == NULL || timeProp[0] == 0)
    timeProp = "observedAt";

  if (treeP->type == KjArray)
  {
    for (KjNode* itemP = treeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
      aggregateEntity(itemP, methodsV, periodNs, startNs, endNs, timeProp, kjsonP, faP);
  }
  else
  {
    aggregateEntity(treeP, methodsV, periodNs, startNs, endNs, timeProp, kjsonP, faP);
  }
}
