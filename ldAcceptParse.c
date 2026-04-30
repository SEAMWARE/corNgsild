//
// FILE            ldAcceptParse.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stddef.h>                                      // NULL
#include <stdbool.h>                                     // bool
#include <stdlib.h>                                      // strtod
#include <string.h>                                      // strchr, strncasecmp, strcasecmp, strncmp

#include "swNgsild/ldAcceptParse.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// trimSpaces - advance past leading whitespace
//
static const char* trimSpaces(const char* s)
{
  while (*s == ' ' || *s == '\t')
    s++;
  return s;
}



// -----------------------------------------------------------------------------
//
// nameMatches - case-insensitive prefix-equality of mediaType against full,
//               with end-of-token guard. tokenEnd points one past the last
//               char of mediaType in the source string.
//
static bool nameMatches(const char* tokenStart, const char* tokenEnd, const char* full)
{
  int mLen = (int)(tokenEnd - tokenStart);
  int fLen = (int) strlen(full);
  if (mLen != fLen) return false;
  return strncasecmp(tokenStart, full, mLen) == 0;
}



// -----------------------------------------------------------------------------
//
// extractQ - parse the q= parameter (if any) of a single Accept entry.
//
// `params` points just past the media type, at the first ';' or end. Default
// q = 1.0 when no q= is present. Out-of-range q values are clamped to [0,1].
//
static double extractQ(const char* params, const char* entryEnd)
{
  const char* p = params;
  while (p < entryEnd)
  {
    while (p < entryEnd && (*p == ' ' || *p == '\t' || *p == ';')) p++;

    if (p + 2 < entryEnd && (p[0] == 'q' || p[0] == 'Q') && p[1] == '=')
    {
      char buf[16];
      int  bn = 0;
      const char* v = p + 2;
      while (v < entryEnd && bn < (int)(sizeof(buf) - 1) && *v != ';' && *v != ' ' && *v != '\t')
        buf[bn++] = *v++;
      buf[bn] = 0;
      double q = strtod(buf, NULL);
      if (q < 0) q = 0;
      if (q > 1) q = 1;
      return q;
    }

    // Skip until next ';' or end
    while (p < entryEnd && *p != ';') p++;
  }

  return 1.0;
}



// -----------------------------------------------------------------------------
//
// ldAcceptParse -
//
LdAcceptType ldAcceptParse(const char* acceptHeader)
{
  if (acceptHeader == NULL || *acceptHeader == 0)
    return LdAcceptJson;

  // Track best-q-so-far per type. -1.0 means "not seen / excluded".
  double qJson    = -1.0;
  double qLdJson  = -1.0;
  double qGeoJson = -1.0;

  // Order-of-first-appearance per type, for tie-breaking on equal q.
  int orderJson    = -1;
  int orderLdJson  = -1;
  int orderGeoJson = -1;

  const char* p     = acceptHeader;
  int         entry = 0;

  while (*p != 0)
  {
    p = trimSpaces(p);
    if (*p == 0) break;

    // Locate end of this entry (next comma or end-of-string).
    const char* entryEnd = p;
    while (*entryEnd != 0 && *entryEnd != ',') entryEnd++;

    // Locate end of media-type name (within the entry: up to ';' or entryEnd).
    const char* nameEnd = p;
    while (nameEnd < entryEnd && *nameEnd != ';' && *nameEnd != ' ' && *nameEnd != '\t') nameEnd++;

    double q = extractQ(nameEnd, entryEnd);

    LdAcceptType t  = (LdAcceptType) -1;
    bool        wildcardJson = false;

    if      (nameMatches(p, nameEnd, "application/geo+json")) t = LdAcceptGeoJson;
    else if (nameMatches(p, nameEnd, "application/ld+json"))  t = LdAcceptLdJson;
    else if (nameMatches(p, nameEnd, "application/json"))     t = LdAcceptJson;
    else if (nameMatches(p, nameEnd, "application/*") ||
             nameMatches(p, nameEnd, "*/*"))                  wildcardJson = true;

    if (t == LdAcceptGeoJson)
    {
      if (q > qGeoJson) { qGeoJson = q; orderGeoJson = entry; }
    }
    else if (t == LdAcceptLdJson)
    {
      if (q > qLdJson)  { qLdJson  = q; orderLdJson  = entry; }
    }
    else if (t == LdAcceptJson || wildcardJson)
    {
      if (q > qJson)    { qJson    = q; orderJson    = entry; }
    }

    p = entryEnd;
    if (*p == ',') p++;
    entry++;
  }

  // q == 0 is "not acceptable" — exclude from candidates.
  if (qJson    == 0) qJson    = -1.0;
  if (qLdJson  == 0) qLdJson  = -1.0;
  if (qGeoJson == 0) qGeoJson = -1.0;

  // No acceptable type → fall back to the spec's default representation.
  if (qJson < 0 && qLdJson < 0 && qGeoJson < 0)
    return LdAcceptJson;

  // Pick the highest q. Equal q → smallest first-appearance order wins.
  // Initialise with json as the spec-default to bias ties towards it
  // when nothing was sent (which extractQ would have given q=1.0).
  LdAcceptType best  = LdAcceptJson;
  double       bestQ = qJson;
  int          bestO = orderJson;

  if (qLdJson > bestQ || (qLdJson == bestQ && qLdJson >= 0 && (bestO < 0 || orderLdJson < bestO)))
  {
    best = LdAcceptLdJson; bestQ = qLdJson; bestO = orderLdJson;
  }
  if (qGeoJson > bestQ || (qGeoJson == bestQ && qGeoJson >= 0 && (bestO < 0 || orderGeoJson < bestO)))
  {
    best = LdAcceptGeoJson;
  }

  return best;
}
