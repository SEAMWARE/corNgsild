//
// FILE            ldParamsValidate.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                     // bool

#include "swNgsild/SwNgsild.h"                           // swNgsild
#include "swNgsild/LdProblem.h"                          // LD_ERROR_BAD_REQUEST_DATA
#include "swNgsild/ldError.h"                            // ldError

#include "swNgsild/ldParamsValidate.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// ldParamsValidate - validate cross-parameter constraints on URL params
//
bool ldParamsValidate(void)
{
  // limit=0 is only valid when count=true (NGSI-LD spec clause 6.3.10)
  if (swNgsild.limit == 0 && swNgsild.count == false)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid request", "limit=0 is only valid when count=true");
    return true;
  }

  return false;
}
