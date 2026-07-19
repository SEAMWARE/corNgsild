//
// FILE            ldCheckDateTime.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#define _GNU_SOURCE
#include <ctype.h>                                       // isdigit
#include <stdlib.h>                                      // mktime
#include <string.h>                                      // strlen, memset
#include <stdint.h>                                      // int64_t
#include <time.h>                                        // struct tm, timegm, strptime

#include "swNgsild/ldCheckDateTime.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// ldCheckDateTime - validate (and optionally parse) an ISO 8601 datetime string
//
// Returns true if the string is a valid, representable DateTime. When secondsP
// is non-NULL it receives the epoch seconds. Accepts:
//   YYYY-MM-DDThh:mm:ss[.sss][Z | +/-hh:mm]; a missing timezone is UTC.
//
// A bool result (not a sentinel) is deliberate: epoch-ns is signed, so -1.0s
// (1969-12-31T23:59:59Z) is a perfectly valid instant and could never have been
// distinguished from an old "-1.0 == error" return.
//
bool ldCheckDateTime(const char* dateTimeStr, double* secondsP)
{
  if (dateTimeStr == NULL)
    return false;

  int len = strlen(dateTimeStr);

  // Minimum: "YYYY-MM-DDThh:mm:ss" = 19 chars (timezone optional)
  if (len < 19)
    return false;

  // Check format: YYYY-MM-DDThh:mm:ss
  if (!isdigit(dateTimeStr[0])  || !isdigit(dateTimeStr[1])  || !isdigit(dateTimeStr[2])  || !isdigit(dateTimeStr[3])  ||
      dateTimeStr[4] != '-'     ||
      !isdigit(dateTimeStr[5])  || !isdigit(dateTimeStr[6])  ||
      dateTimeStr[7] != '-'     ||
      !isdigit(dateTimeStr[8])  || !isdigit(dateTimeStr[9])  ||
      dateTimeStr[10] != 'T'    ||
      !isdigit(dateTimeStr[11]) || !isdigit(dateTimeStr[12]) ||
      dateTimeStr[13] != ':'    ||
      !isdigit(dateTimeStr[14]) || !isdigit(dateTimeStr[15]) ||
      dateTimeStr[16] != ':'    ||
      !isdigit(dateTimeStr[17]) || !isdigit(dateTimeStr[18]))
  {
    return false;
  }

  struct tm tm;

  memset(&tm, 0, sizeof(tm));
  tm.tm_year = (dateTimeStr[0] - '0') * 1000 + (dateTimeStr[1] - '0') * 100 + (dateTimeStr[2] - '0') * 10 + (dateTimeStr[3] - '0') - 1900;
  tm.tm_mon  = (dateTimeStr[5] - '0') * 10 + (dateTimeStr[6] - '0') - 1;
  tm.tm_mday = (dateTimeStr[8] - '0') * 10 + (dateTimeStr[9] - '0');
  tm.tm_hour = (dateTimeStr[11] - '0') * 10 + (dateTimeStr[12] - '0');
  tm.tm_min  = (dateTimeStr[14] - '0') * 10 + (dateTimeStr[15] - '0');
  tm.tm_sec  = (dateTimeStr[17] - '0') * 10 + (dateTimeStr[18] - '0');

  // Basic range checks
  if (tm.tm_mon < 0 || tm.tm_mon > 11)   return false;

  // Day-of-month must be valid for the specific month and year — April has 30
  // days, February 28 (29 in a leap year). A bare 1..31 range check lets
  // 2023-02-31 / 2023-04-31 / 2023-02-29 through, and timegm() then silently
  // normalizes them to a different instant which is stored as the timestamp.
  {
    static const int daysInMonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int  year   = tm.tm_year + 1900;
    int  maxDay = daysInMonth[tm.tm_mon];
    if (tm.tm_mon == 1)  // February
    {
      bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
      if (leap)
        maxDay = 29;
    }
    if (tm.tm_mday < 1 || tm.tm_mday > maxDay)
      return false;
  }

  if (tm.tm_hour > 23)                    return false;
  if (tm.tm_min > 59)                     return false;
  if (tm.tm_sec > 60)                     return false;  // 60 for leap second

  // Epoch-nanoseconds is a signed int64, representable only from
  // 1677-09-21T00:12:44Z to 2262-04-11T23:47:16Z. A DateTime outside that
  // window overflows the seconds→nanoseconds multiply and wraps to a garbage
  // instant, so reject it (→ 400). The two boundary years straddle the limit,
  // so the safe whole-year window is [1678, 2261] — conservatively reject the
  // boundary years rather than allow their out-of-range parts.
  {
    int year = tm.tm_year + 1900;
    if (year < 1678 || year > 2261)
      return false;
  }

  // Check timezone: must end with 'Z' or '+/-hh:mm'
  const char* tz = dateTimeStr + 19;

  // Skip optional fractional seconds. § 5.2.2.4: the decimal fraction is
  // "one or more fractional digits, up to a maximum of six" — so 1..6 digits.
  if (*tz == '.')
  {
    tz++;
    int fracDigits = 0;
    while (isdigit(*tz))
    {
      tz++;
      fracDigits++;
    }
    if (fracDigits < 1 || fracDigits > 6)
      return false;
  }

  if (*tz == 0)
  {
    // No timezone — defaults to UTC (ISO 8601). timegm() below treats tm as UTC.
  }
  else if (*tz == 'Z' && *(tz + 1) == 0)
  {
    // UTC
  }
  else if ((*tz == '+' || *tz == '-') && strlen(tz) >= 6)
  {
    // Timezone offset
  }
  else
  {
    return false;
  }

  // Valid and representable — hand back the epoch (seconds) via the single
  // converter so the timezone offset is applied consistently.
  if (secondsP != NULL)
    *secondsP = (double) ldIsoToNanoseconds(dateTimeStr) / 1000000000.0;

  return true;
}



// -----------------------------------------------------------------------------
//
// ldIsoToNanoseconds - convert ISO 8601 date-time string to epoch nanoseconds
//
int64_t ldIsoToNanoseconds(const char* iso)
{
  if (iso == NULL)
    return 0;

  struct tm tm;
  memset(&tm, 0, sizeof(tm));

  const char* rest = strptime(iso, "%Y-%m-%dT%H:%M:%S", &tm);
  if (rest == NULL)
    return 0;

  // Signed: timegm is negative for pre-1970 instants, and epoch-ns is stored and
  // compared as signed int64 (range ~1677..2262) so negatives order correctly.
  int64_t ns = (int64_t) timegm(&tm) * 1000000000LL;

  if (*rest == '.')
  {
    rest++;
    int64_t frac   = 0;
    int     digits = 0;
    while (*rest >= '0' && *rest <= '9' && digits < 9)
    {
      frac = frac * 10 + (*rest - '0');
      rest++;
      digits++;
    }
    while (digits < 9)
    {
      frac *= 10;
      digits++;
    }
    ns += frac;
  }

  //
  // Trailing timezone designator: 'Z' (or none) is UTC; '+hh:mm' / '-hh:mm' is
  // an offset. strptime+timegm above computed the wall-clock as if it were UTC,
  // so for an explicit offset convert to true UTC: UTC = local - offset.
  //
  if ((*rest == '+') || (*rest == '-'))
  {
    int sign = (*rest == '+') ? 1 : -1;
    rest++;

    int oh = 0, om = 0;
    if ((rest[0] >= '0' && rest[0] <= '9') && (rest[1] >= '0' && rest[1] <= '9'))
    {
      oh = (rest[0] - '0') * 10 + (rest[1] - '0');
      rest += 2;
      if (*rest == ':')
        rest++;
      if ((rest[0] >= '0' && rest[0] <= '9') && (rest[1] >= '0' && rest[1] <= '9'))
        om = (rest[0] - '0') * 10 + (rest[1] - '0');
    }

    int64_t offsetNs = (int64_t) (oh * 3600 + om * 60) * 1000000000LL * sign;
    ns -= offsetNs;
  }

  return ns;
}
