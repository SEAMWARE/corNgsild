//
// FILE            ldInit.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool

#include "kbase/kLibLog.h"                             // KLOG_T
#include "swJsonld/swldExpand.h"                       // swldSetVocabExpandCheck

#include "swNgsild/ldTraceLevels.h"                      // LdTInit
#include "swNgsild/ldParams.h"                           // ldParamsInit
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/LdProblem.h"                          // LD_ERROR_BAD_REQUEST_DATA
#include "swNgsild/ldHooks.h"                            // ldHooksRegister
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
