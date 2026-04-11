#ifndef SWNGSILD_LDCHECKDATETIME_H_
#define SWNGSILD_LDCHECKDATETIME_H_

//
// FILE            ldCheckDateTime.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
//
#include <stdint.h>                                   // uint64_t



// -----------------------------------------------------------------------------
//
// ldCheckDateTime -
//
// Returns epoch seconds on success, -1.0 on failure.
//
extern double ldCheckDateTime(const char* dateTimeStr);



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
extern uint64_t ldIsoToNanoseconds(const char* iso);

#endif  // SWNGSILD_LDCHECKDATETIME_H_
