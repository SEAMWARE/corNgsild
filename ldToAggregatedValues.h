#ifndef SWNGSILD_LDTOAGGREGATEDVALUES_H_
#define SWNGSILD_LDTOAGGREGATEDVALUES_H_

//
// FILE            ldToAggregatedValues.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                    // bool

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
                                 uint64_t      periodNs,
                                 uint64_t      startNs,
                                 uint64_t      endNs,
                                 const char*   timeProp,
                                 Kjson*        kjsonP,
                                 KAlloc*       faP);



// -----------------------------------------------------------------------------
//
// ldIso8601DurationToNs - parse an ISO 8601 duration string into nanoseconds.
//
// Supports PnYnMnDTnHnMnS — but only D / H / M / S parts are honored. Y / M
// (months) are rejected (their length isn't fixed). Returns 0 on parse error
// or when the duration is zero.
//
extern uint64_t ldIso8601DurationToNs(const char* iso);



// -----------------------------------------------------------------------------
//
// ldAggrMethodValid - is 's' a recognised aggregation method?
//
// The § 3.2.7 closed enum (avg, distinctCount, max, min, stddev, sum, sumsq,
// totalCount) plus the broker's documented "count" alias for totalCount.
//
extern bool ldAggrMethodValid(const char* s);

#endif  // SWNGSILD_LDTOAGGREGATEDVALUES_H_
