//
// FILE            ldError.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//

#include <stdarg.h>                               // va_list, va_start, va_end
#include <stdio.h>                                // vsnprintf

#include "kbase/kLibLog.h"                      // kLogFunction
#include "swRest/swRest.h"                      // swRest

#include "ldError.h"                              // Own interface



// -----------------------------------------------------------------------------
//
// ldErrorFunction -
//
void ldErrorFunction
(
  int          status,
  const char*  type,
  const char*  title,
  const char*  fileName,
  int          lineNo,
  const char*  functionName,
  const char*  fmt,
  ...
)
{
  swRest.out.httpStatusCode = status;
  swRest.out.problemType   = type;
  swRest.out.problemTitle  = title;

  va_list ap;

  va_start(ap, fmt);
  vsnprintf(swRest.out.problemDetail, sizeof(swRest.out.problemDetail), fmt, ap);
  va_end(ap);

  //
  // Log the error at the caller's location
  //
  if (kLogFunction != NULL)
    kLogFunction(1, 0, fileName, lineNo, functionName, "%d %s: %s", status, title, swRest.out.problemDetail);
}
