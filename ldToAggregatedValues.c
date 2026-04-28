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
// nsToIso - format an epoch-ns timestamp as ISO 8601 (UTC, ms precision).
//
static char* nsToIso(uint64_t ns, KAlloc* faP)
{
  const int sz = 64;
  char* buf = (char*) kaAlloc(faP, sz);
  time_t t  = (time_t) (ns / 1000000000ULL);
  long   us = (long) ((ns % 1000000000ULL) / 1000);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  snprintf(buf, sz, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
           tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
           tmv.tm_hour, tmv.tm_min, tmv.tm_sec, us / 1000);
  return buf;
}



// -----------------------------------------------------------------------------
//
// Bucket - per-period accumulator (fixed-size growable array).
//
typedef struct Bucket
{
  uint64_t  startNs;
  int       count;
  double    sum;
  double    sumsq;
  double    minV;
  double    maxV;

  // For distinctCount: keep up to a small handful of distinct values.
  // For numeric Property this is rarely useful, but the spec requires the
  // method to be supported. Cap to keep this simple.
  double    distinctV[64];
  int       distinctN;
} Bucket;



static void bucketInit(Bucket* b, uint64_t startNs)
{
  memset(b, 0, sizeof(*b));
  b->startNs = startNs;
}



static void bucketAdd(Bucket* b, double v)
{
  if (b->count == 0)
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
  b->count++;

  if (b->distinctN < (int)(sizeof(b->distinctV)/sizeof(b->distinctV[0])))
  {
    bool seen = false;
    for (int i = 0; i < b->distinctN; i++)
      if (b->distinctV[i] == v) { seen = true; break; }
    if (!seen)
      b->distinctV[b->distinctN++] = v;
  }
}



static double bucketStddev(const Bucket* b)
{
  if (b->count < 2) return 0.0;
  double mean = b->sum / b->count;
  double var  = (b->sumsq / b->count) - (mean * mean);
  return (var > 0.0) ? sqrt(var) : 0.0;
}



// -----------------------------------------------------------------------------
//
// extractNumber - read a numeric value out of a "value" KjNode.
//
// Returns true with *out filled when the node is numeric, false otherwise
// (non-Property attrs, or Property with non-numeric value).
//
static bool extractNumber(KjNode* valP, double* out)
{
  if (valP == NULL) return false;
  if (valP->type == KjInt)   { *out = (double) valP->value.i; return true; }
  if (valP->type == KjFloat) { *out = valP->value.f;          return true; }
  return false;
}



// -----------------------------------------------------------------------------
//
// emitValueArray - build [[v, ts-start, ts-end], ...] for one method.
//
static KjNode* emitValueArray(const char*   method,
                              Bucket*       buckets,
                              int           bucketCount,
                              uint64_t      periodNs,
                              Kjson*        kjsonP,
                              KAlloc*       faP)
{
  KjNode* arr = kjArray(kjsonP, method);

  for (int i = 0; i < bucketCount; i++)
  {
    Bucket* b = &buckets[i];
    if (b->count == 0)
      continue;

    double v;
    if      (strcmp(method, "avg")           == 0)  v = b->sum / b->count;
    else if (strcmp(method, "sum")           == 0)  v = b->sum;
    else if (strcmp(method, "min")           == 0)  v = b->minV;
    else if (strcmp(method, "max")           == 0)  v = b->maxV;
    else if (strcmp(method, "totalCount")    == 0)  v = (double) b->count;
    else if (strcmp(method, "count")         == 0)  v = (double) b->count;
    else if (strcmp(method, "distinctCount") == 0)  v = (double) b->distinctN;
    else if (strcmp(method, "stddev")        == 0)  v = bucketStddev(b);
    else if (strcmp(method, "sumsq")         == 0)  v = b->sumsq;
    else                                            continue;

    KjNode* tuple = kjArray(kjsonP, NULL);
    kjChildAdd(tuple, kjFloat(kjsonP, NULL, v));
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

  if (strcmp(attrType, "Property") != 0)
    return;  // numeric aggregation only on Property in v1

  // Establish window end if caller passed 0 → take latest sample seen.
  uint64_t winEnd = endNs;
  if (winEnd == 0)
  {
    for (KjNode* instP = firstP; instP != NULL; instP = instP->next)
    {
      if (instP->type != KjObject) continue;
      KjNode* tsP = kjLookup(instP, timeProp);
      if (tsP == NULL || tsP->type != KjString) continue;
      uint64_t ts = isoToNs(tsP->value.s);
      if (ts > winEnd) winEnd = ts;
    }
  }
  if (winEnd <= startNs || periodNs == 0)
    return;

  int bucketCount = (int) ((winEnd - startNs + periodNs - 1) / periodNs);
  if (bucketCount <= 0) bucketCount = 1;

  Bucket* buckets = (Bucket*) kaAlloc(faP, sizeof(Bucket) * bucketCount);
  for (int i = 0; i < bucketCount; i++)
    bucketInit(&buckets[i], startNs + (uint64_t) i * periodNs);

  // Second pass: bucket the instances.
  for (KjNode* instP = firstP; instP != NULL; instP = instP->next)
  {
    if (instP->type != KjObject) continue;
    double v;
    if (!extractNumber(kjLookup(instP, "value"), &v)) continue;

    KjNode* tsP = kjLookup(instP, timeProp);
    if (tsP == NULL || tsP->type != KjString) continue;
    uint64_t ts = isoToNs(tsP->value.s);
    if (ts < startNs || ts >= winEnd) continue;

    int idx = (int) ((ts - startNs) / periodNs);
    if (idx < 0 || idx >= bucketCount) continue;
    bucketAdd(&buckets[idx], v);
  }

  // Replace the array contents with { type, "<method>": [...], ... }.
  attrP->type              = KjObject;
  attrP->value.firstChildP = NULL;
  attrP->lastChild         = NULL;
  kjChildAdd(attrP, kjString(kjsonP, "type", attrType));

  for (int m = 0; methodsV != NULL && methodsV[m] != NULL; m++)
  {
    KjNode* methodArr = emitValueArray(methodsV[m], buckets, bucketCount, periodNs, kjsonP, faP);
    kjChildAdd(attrP, methodArr);
  }
}



// -----------------------------------------------------------------------------
//
// aggregateEntity - apply aggregateAttr to every numeric Property of one entity.
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
