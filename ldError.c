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
#include "kjson/KjNode.h"                       // KjNode
#include "kjson/kjBuilder.h"                    // kjObject, kjString, kjChildAdd
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



// -----------------------------------------------------------------------------
//
// ldErrorExtraString -
//
void ldErrorExtraString(const char* name, const char* value)
{
  if (value == NULL)
    return;

  if (swRest.out.problemExtras == NULL)
    swRest.out.problemExtras = kjObject(swRest.kjsonP, NULL);

  kjChildAdd(swRest.out.problemExtras, kjString(swRest.kjsonP, name, value));
}



// -----------------------------------------------------------------------------
//
// ldErrorExtraInt -
//
void ldErrorExtraInt(const char* name, int value)
{
  if (swRest.out.problemExtras == NULL)
    swRest.out.problemExtras = kjObject(swRest.kjsonP, NULL);

  kjChildAdd(swRest.out.problemExtras, kjInteger(swRest.kjsonP, name, value));
}
