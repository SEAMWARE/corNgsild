#ifndef CORNGSILD_LDISO8601DURATION_H_
#define CORNGSILD_LDISO8601DURATION_H_

//
// FILE            ldIso8601Duration.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// ISO 8601 duration parser — used for snapshotLifetime (§ 5.2.41).
//
// Form: P[nY][nM][nD][T[nH][nM][nS]] where each n is a non-negative
// integer or fixed-point decimal. The spec calls for xsd:duration but
// for snapshot-lifetime purposes we accept the common subset and treat
// 1Y=365d / 1Mo=30d (calendar variability irrelevant to expiry math).
//
// Returns the parsed duration in nanoseconds, or -1 on parse error /
// non-positive input.
//
#include <stdint.h>                                      // int64_t


extern int64_t ldIso8601DurationParseNs(const char* s);

#endif  // CORNGSILD_LDISO8601DURATION_H_
