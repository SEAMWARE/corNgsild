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
#include "swNgsild/ldCheckDateTime.h"                    // ldIsoToNanoseconds
#include "swNgsild/ldToAggregatedValues.h"               // Own interface



// -----------------------------------------------------------------------------
//
// ldAggrMethodValid - is 's' a recognised aggregation method (§ 3.2.7)?
//
bool ldAggrMethodValid(const char* s)
{
  return (strcmp(s, "totalCount")    == 0 ||
          strcmp(s, "count")         == 0 ||   // documented alias for totalCount
          strcmp(s, "distinctCount") == 0 ||
          strcmp(s, "sum")           == 0 ||
          strcmp(s, "sumsq")         == 0 ||
          strcmp(s, "avg")           == 0 ||
          strcmp(s, "min")           == 0 ||
          strcmp(s, "max")           == 0 ||
          strcmp(s, "stddev")        == 0);
}



// -----------------------------------------------------------------------------
//
// ldIso8601DurationParse - parse PnYnMnDTnHnMnS / PnW (§ 5.3.2.7).
//
// Returns false when the string is not a valid duration - "banana", "1D" (no P),
// bare "P", "PT", a number with no unit. That case must NOT be confused with a
// zero duration, which is valid and means "the whole time range".
//
// Splits the result in two, because the two halves bucket differently:
//   months - Y and M, whose length varies -> a calendar boundary walk
//   ns     - W/D/H/M/S, all constant       -> a fixed width
// A duration may carry both (P1M15D); boundaries then step a month, then 15 days.
//
bool ldIso8601DurationParse(const char* iso, LdDuration* durationP)
{
  durationP->months = 0;
  durationP->ns     = 0;

  if (iso == NULL || *iso != 'P')
    return false;

  iso++;
  if (*iso == 0)                                              // bare "P"
    return false;

  bool inTime   = false;
  bool anyField = false;                                      // "PT" alone is invalid too

  while (*iso != 0)
  {
    if (*iso == 'T')
    {
      if (inTime)                                             // a second 'T'
        return false;
      inTime = true;
      iso++;
      continue;
    }

    char*     end = NULL;
    long long n   = strtoll(iso, &end, 10);
    if (end == iso || n < 0)
      return false;

    char unit = *end;
    if (unit == 0)                                            // number with no unit
      return false;
    iso = end + 1;

    if (!inTime)
    {
      if      (unit == 'D')  durationP->ns     += (uint64_t) n * 86400ULL * 1000000000ULL;
      else if (unit == 'W')  durationP->ns     += (uint64_t) n * 7ULL * 86400ULL * 1000000000ULL;
      else if (unit == 'Y')  durationP->months += (uint32_t) n * 12;
      else if (unit == 'M')  durationP->months += (uint32_t) n;
      else                   return false;
    }
    else
    {
      if      (unit == 'H')  durationP->ns += (uint64_t) n * 3600ULL * 1000000000ULL;
      else if (unit == 'M')  durationP->ns += (uint64_t) n * 60ULL   * 1000000000ULL;
      else if (unit == 'S')  durationP->ns += (uint64_t) n * 1000000000ULL;
      else                   return false;
    }

    anyField = true;
  }

  return anyField;
}



// -----------------------------------------------------------------------------
//
// daysInMonth - days in month `mon` (0-11) of year `year` (proleptic Gregorian).
//
static int daysInMonth(int year, int mon)
{
  static const int dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (mon != 1)
    return dim[mon];
  bool leap = ((year % 4) == 0) && (((year % 100) != 0) || ((year % 400) == 0));
  return leap ? 29 : 28;
}



// -----------------------------------------------------------------------------
//
// periodAdvance - the next bucket boundary after `ns`.
//
// Calendar months are added on the broken-down date, then the fixed part in ns.
// The day-of-month is CLAMPED to the target month's length, so 2026-01-31 + P1M
// is 2026-02-28 and not 2026-03-03 (which is what letting timegm() normalise an
// overflowing mday would produce). That clamp is why P1M cannot be expressed as
// a constant nanosecond width, and it keeps boundaries monotonic.
//
static uint64_t periodAdvance(uint64_t ns, uint32_t months, uint64_t fixedNs)
{
  uint64_t out = ns;

  if (months > 0)
  {
    time_t    t    = (time_t) (out / 1000000000ULL);
    uint64_t  frac = out % 1000000000ULL;
    struct tm tmv;
    gmtime_r(&t, &tmv);

    int total   = (tmv.tm_year + 1900) * 12 + tmv.tm_mon + (int) months;
    tmv.tm_year = (total / 12) - 1900;
    tmv.tm_mon  = total % 12;

    int dim = daysInMonth(tmv.tm_year + 1900, tmv.tm_mon);
    if (tmv.tm_mday > dim)
      tmv.tm_mday = dim;

    out = (uint64_t) timegm(&tmv) * 1000000000ULL + frac;
  }

  return out + fixedNs;
}






// -----------------------------------------------------------------------------
//
// isoToNs is provided by ldCheckDateTime (ldIsoToNanoseconds) — the single
// ISO 8601 → epoch-nanoseconds converter (handles fractional seconds and the
// timezone offset). See ldCheckDateTime.h.
#define isoToNs(iso) ldIsoToNanoseconds(iso)



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
  uint64_t  endNs;                                  // exclusive; stored, not derived - a
                                                    // calendar period has no constant width

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



static void bucketInit(Bucket* b, uint64_t startNs, uint64_t endNs)
{
  memset(b, 0, sizeof(*b));
  b->startNs = startNs;
  b->endNs   = endNs;
}



// -----------------------------------------------------------------------------
//
// bucketIndexOf - which bucket does `ts` fall in? -1 = none.
//
// Boundaries are monotonic, so a binary search works for both the fixed-width
// and the calendar case without caring which produced them.
//
static int bucketIndexOf(const Bucket* buckets, int bucketCount, uint64_t ts)
{
  int lo = 0;
  int hi = bucketCount - 1;

  while (lo <= hi)
  {
    int mid = (lo + hi) / 2;
    if      (ts <  buckets[mid].startNs) hi = mid - 1;
    else if (ts >= buckets[mid].endNs)   lo = mid + 1;
    else                                 return mid;
  }

  return -1;
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
    kjChildAdd(tuple, kjString(kjsonP, NULL, nsToIso(b->endNs, faP)));
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
                          uint32_t      periodMonths,
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
  if (winEnd <= winStart)
    return;

  // § 5.3.2.7: aggrPeriodDuration of 0 (PT0S / P0D) — or absent — means ONE
  // bucket spanning the whole [winStart, winEnd) window. Use that width as the
  // bucket period so the rest of the bucketing math (index, "to" timestamp) is
  // uniform; without this a 0 period bailed out and the attr stayed normalized.
  // § 5.3.2.7 bucket boundaries. Three cases:
  //   zero duration      - ONE bucket spanning the whole [winStart, winEnd)
  //   fixed (W/D/H/M/S)  - constant width, so the count is a division
  //   calendar (Y/M)     - width varies per bucket, so the boundaries are walked
  bool zeroPeriod = ((periodMonths == 0) && (periodNs == 0));
  int  bucketCount;

  if (zeroPeriod)
    bucketCount = 1;
  else if (periodMonths == 0)
    bucketCount = (int) ((winEnd - winStart + periodNs - 1) / periodNs);
  else
  {
    bucketCount = 0;
    for (uint64_t b = winStart; b < winEnd; b = periodAdvance(b, periodMonths, periodNs))
      bucketCount++;
  }
  if (bucketCount <= 0) bucketCount = 1;

  Bucket*  buckets = (Bucket*) kaAlloc(faP, sizeof(Bucket) * bucketCount);
  uint64_t boundary = winStart;
  for (int i = 0; i < bucketCount; i++)
  {
    uint64_t next = zeroPeriod ? winEnd : periodAdvance(boundary, periodMonths, periodNs);
    if (next > winEnd)                       // last bucket is clipped to the window
      next = winEnd;
    bucketInit(&buckets[i], boundary, next);
    boundary = next;
  }

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

    int idx = bucketIndexOf(buckets, bucketCount, ts);
    if (idx < 0) continue;
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
                                       attrType, kjsonP, faP);
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
                            uint32_t       periodMonths,
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
    aggregateAttr(childP, methodsV, periodMonths, periodNs, startNs, endNs, timeProp, kjsonP, faP);
  }
}



// -----------------------------------------------------------------------------
//
// ldToAggregatedValues -
//
void ldToAggregatedValues(KjNode*       treeP,
                          char**        methodsV,
                          uint32_t       periodMonths,
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
      aggregateEntity(itemP, methodsV, periodMonths, periodNs, startNs, endNs, timeProp, kjsonP, faP);
  }
  else
  {
    aggregateEntity(treeP, methodsV, periodMonths, periodNs, startNs, endNs, timeProp, kjsonP, faP);
  }
}
