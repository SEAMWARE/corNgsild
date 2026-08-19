//
// FILE            ldNameContentCheck.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// § 4.6.2 / § 4.6.4 validation — see header.
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strchr, strcmp

#include "kjson/KjNode.h"                                // KjNode
#include "kjson/kjLookup.h"                              // kjLookup

#include "corNgsild/ldError.h"                            // ldError
#include "corNgsild/LdProblem.h"                          // LD_ERROR_BAD_REQUEST_DATA
#include "corNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "corNgsild/ldNameContentCheck.h"                 // Own interface


// Top-level entity members that are URIs / DateTimes / structural — not names.
static bool isStructuralKey(const char* key)
{
  if (key == NULL) return false;
  if (key[0] == '@')                                                return true;
  if (strcmp(key, "id")            == 0)                            return true;
  if (strcmp(key, "scope")         == 0)                            return true;
  if (strcmp(key, "createdAt")     == 0)                            return true;
  if (strcmp(key, "modifiedAt")    == 0)                            return true;
  if (strcmp(key, "deletedAt")     == 0)                            return true;
  if (strcmp(key, "expiresAt")     == 0)                            return true;
  if (strcmp(key, "observedAt")    == 0)                            return true;
  if (strcmp(key, "value")         == 0)                            return true;
  if (strcmp(key, "object")        == 0)                            return true;
  if (strcmp(key, "objectType")    == 0)                            return true;
  if (strcmp(key, "languageMap")   == 0)                            return true;
  if (strcmp(key, "valueList")     == 0)                            return true;
  if (strcmp(key, "objectList")    == 0)                            return true;
  if (strcmp(key, "vocab")         == 0)                            return true;
  if (strcmp(key, "json")          == 0)                            return true;
  if (strcmp(key, "datasetId")     == 0)                            return true;
  if (strcmp(key, "unitCode")      == 0)                            return true;
  if (strcmp(key, "valueType")     == 0)                            return true;
  if (strcmp(key, "type")          == 0)                            return true;  // value validated separately
  return false;
}



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



// -----------------------------------------------------------------------------
//
// ldStringHasForbiddenChars -
//
bool ldStringHasForbiddenChars(const char* s)
{
  if (s == NULL) return false;
  for (const unsigned char* p = (const unsigned char*) s; *p != 0; p++)
  {
    switch (*p)
    {
      case '<': case '>': case '"': case '\'':
      case '=': case ';': case '(':  case ')':
        return true;
    }
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// checkValueContent - a Property's `value` may be string, array, or object
// (JSON-LD typed). For strings, run the forbidden-char check; for arrays,
// recurse into elements; for objects, recurse into members (skipping the
// type key). Returns true if clean, false (with ldError set) on violation.
//
static bool checkValueContent(const char* attrName, KjNode* valueP)
{
  if (valueP == NULL) return true;

  if (valueP->type == KjString)
  {
    if (ldStringHasForbiddenChars(valueP->value.s))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Forbidden Characters",
              "value of '%s' contains forbidden characters (< > \" ' = ; ( ) — § 4.6.4)",
              (attrName != NULL) ? attrName : "(anonymous)");
      return false;
    }
    return true;
  }

  if (valueP->type == KjArray || valueP->type == KjObject)
  {
    for (KjNode* c = valueP->value.firstChildP; c != NULL; c = c->next)
    {
      if (!checkValueContent(attrName, c)) return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// checkAttribute - validate one attribute's name + nested values.
//
// Walk a single attribute's nested values and check for forbidden chars
// in any string. Names are NOT checked here — they go through
// ldVocabNameCheck during JSON-LD expansion (only suspect @vocab-fallback
// names get validated). Recurses into sub-attributes for value content.
//
static bool checkAttribute(KjNode* attrP);

static bool checkAttribute(KjNode* attrP)
{
  if (attrP == NULL) return true;

  // Multi-instance attribute: array of instance objects.
  if (attrP->type == KjArray)
  {
    for (KjNode* inst = attrP->value.firstChildP; inst != NULL; inst = inst->next)
    {
      if (inst->type != KjObject) continue;
      KjNode tmp = *inst;
      tmp.name = attrP->name;
      if (!checkAttribute(&tmp)) return false;
    }
    return true;
  }

  if (attrP->type != KjObject) return true;

  for (KjNode* c = attrP->value.firstChildP; c != NULL; c = c->next)
  {
    if (c->name == NULL) continue;

    if (strcmp(c->name, "value") == 0)
    {
      if (!checkValueContent(attrP->name, c)) return false;
      continue;
    }

    if (isStructuralKey(c->name)) continue;

    // Sub-attribute: recurse to scan its values. Name itself is
    // covered by ldVocabNameCheck during JSON-LD expansion.
    if (c->type == KjObject || c->type == KjArray)
      if (!checkAttribute(c)) return false;
  }
  return true;
}



// -----------------------------------------------------------------------------
//
// checkEntity - walk attributes scanning string values for § 4.6.4
// forbidden chars. Names (entity type, attribute names) are validated
// by ldVocabNameCheck during JSON-LD expansion — only names that fall
// through to @vocab go through that hook, which is exactly the suspect
// set (user-defined names without a proper @context mapping).
//
static bool checkEntity(KjNode* entityP)
{
  if (entityP == NULL || entityP->type != KjObject) return true;

  for (KjNode* c = entityP->value.firstChildP; c != NULL; c = c->next)
  {
    if (c->name == NULL) continue;
    if (isStructuralKey(c->name)) continue;
    if (!checkAttribute(c)) return false;
  }
  return true;
}



// -----------------------------------------------------------------------------
//
// ldCheckNamesAndContent -
//
bool ldCheckNamesAndContent(KjNode* tree)
{
  if (tree == NULL) return true;

  // Batch payload: array of entity objects.
  if (tree->type == KjArray)
  {
    for (KjNode* e = tree->value.firstChildP; e != NULL; e = e->next)
    {
      if (e->type == KjObject)
        if (!checkEntity(e)) return false;
    }
    return true;
  }

  if (tree->type == KjObject)
    return checkEntity(tree);

  return true;
}
