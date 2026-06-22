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
#include <time.h>                                        // struct tm, timegm, strptime

#include "swNgsild/ldCheckDateTime.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// ldCheckDateTime - validate and parse an ISO 8601 datetime string
//
// Returns epoch seconds on success, -1.0 on failure.
// Accepts: YYYY-MM-DDThh:mm:ss[.sss][Z | +/-hh:mm].
// A missing timezone defaults to UTC (ISO 8601 / § 4.6.x).
//
double ldCheckDateTime(const char* dateTimeStr)
{
  if (dateTimeStr == NULL)
    return -1.0;

  int len = strlen(dateTimeStr);

  // Minimum: "YYYY-MM-DDThh:mm:ss" = 19 chars (timezone optional)
  if (len < 19)
    return -1.0;

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
    return -1.0;
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
  if (tm.tm_mon < 0 || tm.tm_mon > 11)   return -1.0;
  if (tm.tm_mday < 1 || tm.tm_mday > 31) return -1.0;
  if (tm.tm_hour > 23)                    return -1.0;
  if (tm.tm_min > 59)                     return -1.0;
  if (tm.tm_sec > 60)                     return -1.0;  // 60 for leap second

  // Check timezone: must end with 'Z' or '+/-hh:mm'
  const char* tz = dateTimeStr + 19;

  // Skip optional fractional seconds
  if (*tz == '.')
  {
    tz++;
    while (isdigit(*tz))
      tz++;
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
    return -1.0;
  }

  // Lexically valid — return the epoch (seconds) via the single converter so the
  // timezone offset is applied consistently (callers such as expiresAt use it).
  return (double) ldIsoToNanoseconds(dateTimeStr) / 1000000000.0;
}



// -----------------------------------------------------------------------------
//
// ldIsoToNanoseconds - convert ISO 8601 date-time string to epoch nanoseconds
//
uint64_t ldIsoToNanoseconds(const char* iso)
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
    uint64_t frac   = 0;
    int      digits = 0;
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

    long long offsetNs = (long long) (oh * 3600 + om * 60) * 1000000000LL * sign;
    ns = (uint64_t) ((long long) ns - offsetNs);
  }

  return ns;
}
