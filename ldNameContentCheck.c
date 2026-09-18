//
// FILE            ldNameContentCheck.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// § 5.2.2.3 name validation — see header.
//
// The § 5.2.2.5 VALUE scan that used to live here is gone. That clause names
// < > " ' = ; ( ) as a hazard in the content of VALUES and says implementations
// "should decide how to resolve" it — while also requiring, in the same
// paragraph, that they "shall preserve the representation of the content of the
// values ... and return the original content". Raising BadRequestData is one
// permitted resolution and we took it; the cost was that `Beany's Animal
// Collar` could not be stored, and 81 rows of the FIWARE tutorials' own seed
// data would not load.
//
// KZ 2026-09-18: the restrictions belong on NAMES. Names are a grammar
// (§ 5.2.2.3, ldIsValidName below) which excludes every one of those characters
// by construction, so nothing is lost there.
//
// Injection safety does not depend on the scan: every place a value can become
// query TEXT escapes it at that boundary — jsonStrEscape for the mongoc $expr
// JSON, mongocEscapeDotsInKey for field names, PQexecParams for timescale
// writes, escapeSqlLit for the TRoE q-to-SQL. Nothing is stored escaped, which
// is what keeps it simple: the escape lives inside one statement and dies with
// it.
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strchr, strcmp

#include "kjson/KjNode.h"                                // KjNode
#include "kjson/kjLookup.h"                              // kjLookup

#include "corNgsild/ldError.h"                            // ldError
#include "corNgsild/LdProblem.h"                          // LD_ERROR_BAD_REQUEST_DATA
#include "corNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "corNgsild/ldNameContentCheck.h"                 // Own interface


// -----------------------------------------------------------------------------
//
// nameCharOk - true if the first byte of a UTF-8 sequence at `p` is a valid
// nameChar (Letter / Number / "_"). When `firstChar` is true, "_" and digits
// are rejected (name must start with Letter).
//
// Pragmatic Unicode handling: ASCII letters/digits/underscore are checked
// directly; non-ASCII bytes (0x80+) are accepted as candidates for Letter
// or Number — full UCD classification is out of scope for a defensive
// validator. *advLen returns the number of bytes the caller should skip.
//
static bool nameCharOk(const unsigned char* p, bool firstChar, int* advLen)
{
  unsigned char c = p[0];

  if (c < 0x80)
  {
    *advLen = 1;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))  return true;
    if (firstChar) return false;
    // Spec § 4.6.2 strictly allows only Letter | Number | "_". We
    // additionally tolerate '.' and '-' since they are common in
    // real-world attribute names (e.g. "sensor.temp", "foo-bar") and
    // pose no script-injection risk. The clause is should-level.
    if (c == '_' || c == '.' || c == '-')                  return true;
    if (c >= '0' && c <= '9')                              return true;
    return false;
  }

  // Non-ASCII UTF-8 lead byte: skip the multibyte sequence and accept.
  if      ((c & 0xE0) == 0xC0)  *advLen = 2;
  else if ((c & 0xF0) == 0xE0)  *advLen = 3;
  else if ((c & 0xF8) == 0xF0)  *advLen = 4;
  else                          *advLen = 1;  // malformed lead → consume one byte
  return true;
}



// -----------------------------------------------------------------------------
//
// nameSegmentOk - true if `[start, end)` matches `unicodeLetter *nameChar`.
//
static bool nameSegmentOk(const char* start, const char* end)
{
  if (start == NULL || end == NULL || end <= start) return false;

  const unsigned char* p = (const unsigned char*) start;
  int adv = 0;

  // First char must be a Letter.
  if (!nameCharOk(p, true, &adv)) return false;
  p += adv;

  while ((const char*) p < end)
  {
    if (!nameCharOk(p, false, &adv)) return false;
    p += adv;
  }
  return true;
}



// -----------------------------------------------------------------------------
//
// ldIsValidName -
//
bool ldIsValidName(const char* name)
{
  if (name == NULL || name[0] == 0) return false;

  // IRIs (post-JSON-LD expansion) are accepted unconditionally — URI
  // rules apply, not name rules.
  if (strstr(name, "://") != NULL) return true;

  // Spec § 4.6.2 ABNF defines a single "prefix:name" form, but in
  // practice URN-style multi-segment identifiers (urn:ns:foo:bar) show
  // up as attribute names. We accept any number of ':'-separated
  // segments as long as each segment is a valid name segment and no
  // two colons are consecutive (empty segment).
  size_t len = strlen(name);
  const char* end = name + len;
  const char* segStart = name;

  while (segStart < end)
  {
    const char* colon = strchr(segStart, ':');
    const char* segEnd = (colon != NULL) ? colon : end;
    if (!nameSegmentOk(segStart, segEnd)) return false;
    if (colon == NULL) break;
    segStart = colon + 1;
  }
  return true;
}
