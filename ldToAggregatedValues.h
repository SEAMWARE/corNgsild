#ifndef CORNGSILD_LDTOAGGREGATEDVALUES_H_
#define CORNGSILD_LDTOAGGREGATEDVALUES_H_

//
// FILE            ldToAggregatedValues.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                    // bool
#include <stdint.h>                                     // uint32_t, uint64_t

#include "kalloc/KAlloc.h"                              // KAlloc
#include "kjson/kjson.h"                                // Kjson
#include "kjson/KjNode.h"                               // KjNode



// -----------------------------------------------------------------------------
//
// ldToAggregatedValues - § 4.5.20 aggregated temporal representation.
//
// Takes a temporal entity tree (each attr is a KjArray of instance objects),
// the list of aggregation methods (e.g. ["avg","sum","min","max","count"]),
// the bucket duration in nanoseconds, and the window start/end (epoch ns;
// endNs = 0 means "use the latest sample observed in any attribute"), and
// rewrites each Property attribute to:
//
//   "<attr>": {
//     "type": "Property",
//     "<method>": [[value, ts-start, ts-end], ...],
//     ...
//   }
//
// Non-numeric attrs (Relationship, LanguageProperty, …) are left untouched
// — only Property values can be aggregated in this slice. Empty buckets are
// omitted from the output.
//
// timeProp picks which timestamp axis is used for bucketing: "observedAt"
// (default), "modifiedAt", or "createdAt".
//
// Methods recognised (per spec § 4.5.20): "avg", "sum", "min", "max",
// "totalCount" (alias "count"), "distinctCount", "stddev", "sumsq".
//
extern void ldToAggregatedValues(KjNode*       treeP,
                                 char**        methodsV,
                                 uint32_t      periodMonths,
                                 uint64_t      periodNs,
                                 uint64_t      startNs,
                                 uint64_t      endNs,
                                 const char*   timeProp,
                                 Kjson*        kjsonP,
                                 KAlloc*       faP);



// -----------------------------------------------------------------------------
//
// LdDuration - an ISO 8601 duration split by how it buckets (§ 5.3.2.7).
//
// The two halves cannot be merged: months (from Y and M) have no constant length,
// while ns (from W/D/H/M/S) does. A duration may carry both, e.g. P1M15D.
// months == 0 && ns == 0 is a ZERO duration = one bucket over the whole range.
//
typedef struct LdDuration
{
  uint32_t  months;
  uint64_t  ns;
} LdDuration;



// -----------------------------------------------------------------------------
//
// ldIso8601DurationParse - parse PnYnMnDTnHnMnS or PnW into an LdDuration.
//
// Returns false when the string is not a valid duration; the caller raises 400.
// An invalid duration must never be treated as zero - "the whole time range" is
// what zero MEANS, so conflating them answers a bad request with plausible data.
//
extern bool ldIso8601DurationParse(const char* iso, LdDuration* durationP);



// -----------------------------------------------------------------------------
//
// ldAggrMethodValid - is 's' a recognised aggregation method?
//
// The § 3.2.7 closed enum (avg, distinctCount, max, min, stddev, sum, sumsq,
// totalCount) plus the broker's documented "count" alias for totalCount.
//
extern bool ldAggrMethodValid(const char* s);

#endif  // CORNGSILD_LDTOAGGREGATEDVALUES_H_
