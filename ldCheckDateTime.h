#ifndef CORNGSILD_LDCHECKDATETIME_H_
#define CORNGSILD_LDCHECKDATETIME_H_

//
// FILE            ldCheckDateTime.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
//
#include <stdint.h>                                   // int64_t
#include <stdbool.h>                                  // bool



// -----------------------------------------------------------------------------
//
// ldCheckDateTime -
//
// Returns true if the string is a valid, representable ISO 8601 DateTime
// (epoch-ns fits a signed int64: ~1677-09-21 .. 2262-04-11). When secondsP is
// non-NULL it receives the epoch seconds.
//
extern bool ldCheckDateTime(const char* dateTimeStr, double* secondsP);



// -----------------------------------------------------------------------------
//
// ldIsoToNanoseconds - convert an ISO 8601 date-time string to epoch nanoseconds.
//
// Accepts: YYYY-MM-DDThh:mm:ss[.frac]Z  or  YYYY-MM-DDThh:mm:ss[.frac]+/-hh:mm
// Fractional seconds are honored (up to 9 digits).
//
// Returns 0 for NULL input. Does no validation beyond what strptime tolerates —
// pair with ldCheckDateTime() when the string is coming from untrusted input.
//
extern int64_t  ldIsoToNanoseconds(const char* iso);

#endif  // CORNGSILD_LDCHECKDATETIME_H_
