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
// ldVocabNameCheck - reject short-names with chars that break q-filter syntax
//
// Called by swJsonld when expanding a name via @vocab.
// Forbidden: '=', '[', ']', '&', '?', '"', '\'', control chars, space
//
static bool ldVocabNameCheck(const char* name)
{
  for (const char* p = name; *p != 0; ++p)
  {
    if (*p == '=' || *p == '[' || *p == ']' || *p == '&' || *p == '?' ||
        *p == '"' || *p == '\'' || *p == '\b' || *p == '\t' || *p == '\n' || *p == ' ')
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Attribute Name",
              "attribute name '%s' contains a forbidden character (0x%02x)", name, (unsigned char) *p);
      return false;
    }
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
