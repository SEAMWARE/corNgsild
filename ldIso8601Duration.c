//
// FILE            ldIso8601Duration.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// ISO 8601 duration parser — see header.
//
#include <ctype.h>                                       // isdigit
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // int64_t
#include <stdlib.h>                                      // strtod

#include "corNgsild/ldIso8601Duration.h"                  // Own interface


//
// readNumber - parse a non-negative number (integer or decimal) at *sP.
// Advances *sP past the number on success. Returns -1 on no digit.
//
static double readNumber(const char** sP)
{
  const char* p = *sP;
  if (!isdigit((unsigned char) *p) && *p != '.')
    return -1.0;

  char*  endP = NULL;
  double v    = strtod(p, &endP);
  if (endP == p) return -1.0;
  *sP = endP;
  return v;
}



int64_t ldIso8601DurationParseNs(const char* s)
{
  if (s == NULL || *s != 'P') return -1;
  ++s;

  // 1Y = 365d, 1Mo = 30d — calendar imprecision is fine for expiry math.
  static const int64_t NS_PER_S    = 1000000000LL;
  static const int64_t NS_PER_MIN  = 60LL  * NS_PER_S;
  static const int64_t NS_PER_HR   = 60LL  * NS_PER_MIN;
  static const int64_t NS_PER_DAY  = 24LL  * NS_PER_HR;
  static const int64_t NS_PER_WEEK = 7LL   * NS_PER_DAY;
  static const int64_t NS_PER_MO   = 30LL  * NS_PER_DAY;
  static const int64_t NS_PER_YR   = 365LL * NS_PER_DAY;

  // PnW (week-only) is mutually exclusive with the others per ISO 8601.
  // Allow it as a shortcut: "P2W" = 2 weeks.
  {
    const char* probe = s;
    double      n     = readNumber(&probe);
    if (n >= 0 && *probe == 'W' && *(probe + 1) == 0)
      return (int64_t) (n * (double) NS_PER_WEEK);
  }

  int64_t total    = 0;
  bool    inTime   = false;
  bool    sawAny   = false;

  while (*s != 0)
  {
    if (*s == 'T')
    {
      inTime = true;
      ++s;
      continue;
    }

    double n = readNumber(&s);
    if (n < 0) return -1;
    if (*s == 0) return -1;  // trailing number with no unit

    char unit = *s++;
    int64_t mul = 0;
    if (!inTime)
    {
      switch (unit)
      {
        case 'Y': mul = NS_PER_YR;  break;
        case 'M': mul = NS_PER_MO;  break;
        case 'W': mul = NS_PER_WEEK; break;
        case 'D': mul = NS_PER_DAY; break;
        default:  return -1;
      }
    }
    else
    {
      switch (unit)
      {
        case 'H': mul = NS_PER_HR;  break;
        case 'M': mul = NS_PER_MIN; break;
        case 'S': mul = NS_PER_S;   break;
        default:  return -1;
      }
    }
    total += (int64_t) (n * (double) mul);
    sawAny = true;
  }

  if (!sawAny) return -1;
  if (total <= 0) return -1;
  return total;
}
