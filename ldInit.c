//
// FILE            ldInit.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strcmp, strstr, strlen, strrchr

#include "kbase/kLibLog.h"                             // KLOG_T
#include "kjson/KjNode.h"                              // KjNode
#include "swJsonld/swldExpand.h"                       // swldSetVocabExpandCheck, swldSetValueCheck

#include "swNgsild/ldTraceLevels.h"                      // LdTInit
#include "swNgsild/ldParams.h"                           // ldParamsInit
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldCheckDateTime.h"                    // ldCheckDateTime
#include "swNgsild/ldCheckUri.h"                         // ldCheckUri
#include "swNgsild/LdProblem.h"                          // LD_ERROR_BAD_REQUEST_DATA
#include "swNgsild/ldHooks.h"                            // ldHooksRegister
#include "swNgsild/ldMqttNotify.h"                       // ldMqttInit
#include "swNgsild/ldInit.h"                             // Own interface



// -----------------------------------------------------------------------------
//
// ldVocabNameCheck - validate a name about to be @vocab-expanded
//
// Called by swJsonld when a short name has no @context mapping and is
// about to fall through to @vocab. Names from a user @context or from
// the NGSI-LD core context are pre-validated by their @context author
// and bypass this hook.
//
// § 4.6.2 grammar: unicodeLetter (unicodeLetter | unicodeNumber | "_")*
// — relaxed pragmatically to also allow '.', '-', and ':' for URN-style
// identifiers. The strict spec rule is should-level, this is the
// defense-in-depth interpretation.
//
// Plus the original q-filter / URL guard: '=', '[', ']', '&', '?',
// '"', '\'', control chars, space — these break query syntax.
//
static bool ldVocabNameCheck(const char* name)
{
  if (name == NULL || name[0] == 0) return true;

  // § 4.17 — when the name comes from a subscription's
  // entities[].type or similar field, it can be a type-selection
  // expression with operators `(`, `)`, `|`, `&`, `,`. None of
  // those are legal in a single attribute name (§ 4.6.2 grammar
  // permits only letters / digits / `_` / `.` / `-` / `:`), so if
  // we see any of them the value isn't a single name and the
  // attribute-name rules don't apply. Skip the check; the type-
  // selection parser elsewhere validates the expression itself.
  for (const unsigned char* p = (const unsigned char*) name; *p != 0; ++p)
  {
    if (*p == '(' || *p == ')' || *p == '|' || *p == '&' || *p == ',')
      return true;
  }

  // First char must be a Letter (ASCII letter or non-ASCII high byte).
  unsigned char first = (unsigned char) name[0];
  bool firstOk = (first >= 0x80) ||
                 (first >= 'A' && first <= 'Z') ||
                 (first >= 'a' && first <= 'z');
  if (!firstOk)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Attribute Name",
            "attribute name '%s' must start with a letter (§ 4.6.2)", name);
    return false;
  }

  for (const unsigned char* p = (const unsigned char*) name + 1; *p != 0; ++p)
  {
    unsigned char c = *p;
    if (c >= 0x80)                                     continue;  // non-ASCII passes
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) continue;
    if (c >= '0' && c <= '9')                          continue;
    if (c == '_' || c == '.' || c == '-' || c == ':') continue;

    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Attribute Name",
            "attribute name '%s' contains a forbidden character (0x%02x) — § 4.6.2",
            name, c);
    return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// datatypeMatches - true if the @type string from a term def names the
//                   given XSD datatype (handles short form `xsd:foo` and
//                   the full http://www.w3.org/2001/XMLSchema#foo IRI).
//
static bool datatypeMatches(const char* type, const char* xsdLocal)
{
  if (type == NULL || xsdLocal == NULL) return false;

  if (type[0] == 'x' && type[1] == 's' && type[2] == 'd' && type[3] == ':')
    return strcmp(type + 4, xsdLocal) == 0;

  const char* hash = strrchr(type, '#');
  if (hash != NULL && strstr(type, "XMLSchema") != NULL)
    return strcmp(hash + 1, xsdLocal) == 0;

  return false;
}



// -----------------------------------------------------------------------------
//
// ldValueCheck - validate a value against a term's @type:<datatype>
//
// Invoked by swJsonld during expansion for any term whose @type is set
// and is neither @id nor @vocab. Emits ldError + returns false on
// mismatch.
//
// Supported datatypes (short + full-IRI forms both recognised):
//   xsd:dateTime / xsd:dateTimeStamp   — ISO 8601 datetime
//   xsd:date / xsd:time                — ISO 8601 partials
//   xsd:integer / xsd:int / xsd:long / xsd:short / xsd:byte /
//   xsd:nonNegativeInteger / xsd:positiveInteger
//                                      — JSON int OR numeric string
//   xsd:double / xsd:decimal / xsd:float
//                                      — JSON number OR numeric string
//   xsd:boolean                        — JSON bool OR "true"/"false"
//   xsd:anyURI                         — string passing ldCheckUri
//
// Unknown datatypes pass silently — JSON-LD leaves their semantics to
// the application.
//
static bool isIntegerString(const char* s)
{
  if (s == NULL || *s == 0) return false;
  if (*s == '+' || *s == '-') ++s;
  if (*s == 0) return false;
  for (; *s != 0; ++s) if (*s < '0' || *s > '9') return false;
  return true;
}

static bool isNumberString(const char* s)
{
  if (s == NULL || *s == 0) return false;
  const char* p = s;
  if (*p == '+' || *p == '-') ++p;
  bool sawDigit = false;
  while (*p >= '0' && *p <= '9') { ++p; sawDigit = true; }
  if (*p == '.') { ++p; while (*p >= '0' && *p <= '9') { ++p; sawDigit = true; } }
  if (*p == 'e' || *p == 'E')
  {
    ++p;
    if (*p == '+' || *p == '-') ++p;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') ++p;
  }
  return sawDigit && *p == 0;
}

static bool ldValueCheck(const char* term, const char* datatype, KjNode* valueP)
{
  if (valueP == NULL || term == NULL || datatype == NULL) return true;

  if (datatypeMatches(datatype, "dateTime") || datatypeMatches(datatype, "dateTimeStamp"))
  {
    if (valueP->type != KjString || !ldCheckDateTime(valueP->value.s, NULL))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value",
              "'%s' is bound to @type:%s but value is not a valid ISO 8601 dateTime",
              term, datatype);
      return false;
    }
    return true;
  }

  if (datatypeMatches(datatype, "date"))
  {
    if (valueP->type != KjString || valueP->value.s == NULL || strlen(valueP->value.s) != 10
        || valueP->value.s[4] != '-' || valueP->value.s[7] != '-')
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value",
              "'%s' is bound to @type:%s but value is not a valid xsd:date (YYYY-MM-DD)",
              term, datatype);
      return false;
    }
    return true;
  }

  if (datatypeMatches(datatype, "time"))
  {
    if (valueP->type != KjString || valueP->value.s == NULL
        || strlen(valueP->value.s) < 8 || valueP->value.s[2] != ':' || valueP->value.s[5] != ':')
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value",
              "'%s' is bound to @type:%s but value is not a valid xsd:time (HH:MM:SS)",
              term, datatype);
      return false;
    }
    return true;
  }

  if (datatypeMatches(datatype, "integer") || datatypeMatches(datatype, "int")  ||
      datatypeMatches(datatype, "long")    || datatypeMatches(datatype, "short") ||
      datatypeMatches(datatype, "byte")    || datatypeMatches(datatype, "nonNegativeInteger") ||
      datatypeMatches(datatype, "positiveInteger"))
  {
    if (valueP->type == KjInt) return true;
    if (valueP->type == KjString && isIntegerString(valueP->value.s)) return true;
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value",
            "'%s' is bound to @type:%s but value is not a valid integer", term, datatype);
    return false;
  }

  if (datatypeMatches(datatype, "double")  || datatypeMatches(datatype, "decimal") ||
      datatypeMatches(datatype, "float"))
  {
    if (valueP->type == KjInt || valueP->type == KjFloat) return true;
    if (valueP->type == KjString && isNumberString(valueP->value.s)) return true;
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value",
            "'%s' is bound to @type:%s but value is not a valid number", term, datatype);
    return false;
  }

  if (datatypeMatches(datatype, "boolean"))
  {
    if (valueP->type == KjBoolean) return true;
    if (valueP->type == KjString && valueP->value.s != NULL &&
        (strcmp(valueP->value.s, "true") == 0 || strcmp(valueP->value.s, "false") == 0))
      return true;
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value",
            "'%s' is bound to @type:%s but value is not 'true' or 'false'", term, datatype);
    return false;
  }

  if (datatypeMatches(datatype, "anyURI"))
  {
    if (valueP->type != KjString || ldCheckUri(valueP->value.s) == false)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value",
              "'%s' is bound to @type:%s but value is not a valid URI", term, datatype);
      return false;
    }
    return true;
  }

  // Unknown / unsupported datatype — pass through silently.
  return true;
}



// -----------------------------------------------------------------------------
//
// Global state
//
static bool ldInitialized = false;



// -----------------------------------------------------------------------------
//
// ldInit - initialize swNgsild (register URL params + hooks with swRest)
//
int ldInit(void)
{
  if (ldInitialized == true)
    return 0;

  KLOG_T(LdTInit, "Initializing swNgsild library");

  if (ldParamsInit() == false)
    return -1;

  ldHooksRegister();
  swldSetVocabExpandCheck(ldVocabNameCheck);
  swldSetValueCheck(ldValueCheck);

  // libmosquitto global init for MQTT notifications (§ 7).
  if (ldMqttInit() != 0)
    return -1;

  ldInitialized = true;
  return 0;
}



// -----------------------------------------------------------------------------
//
// ldCleanup -
//
void ldCleanup(void)
{
  if (ldInitialized == false)
    return;

  ldInitialized = false;
}
