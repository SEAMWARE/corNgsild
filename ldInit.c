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

#include "swNgsild/ldTraceLevels.h"                      // LdTInit
#include "swNgsild/ldParams.h"                           // ldParamsInit
#include "swNgsild/ldHooks.h"                            // ldHooksRegister
#include "swNgsild/ldInit.h"                             // Own interface



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
