//
// FILE            ldError.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//

#include <stdarg.h>                               // va_list, va_start, va_end
#include <stdio.h>                                // vsnprintf

#include "ktrace/ktOut.h"                       // ktOut
#include "kjson/KjNode.h"                       // KjNode
#include "kjson/kjBuilder.h"                    // kjObject, kjString, kjChildAdd
#include "corRest/corRest.h"                      // corRest

#include "corNgsild/CorNgsild.h"                   // corNgsild (geoConflictAttr)
#include "corNgsild/LdProblem.h"                  // LD_ERROR_CONFLICT

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
  corRest.out.httpStatusCode = status;
  corRest.out.problemType   = type;
  corRest.out.problemTitle  = title;

  va_list ap;

  va_start(ap, fmt);
  vsnprintf(corRest.out.problemDetail, sizeof(corRest.out.problemDetail), fmt, ap);
  va_end(ap);

  //
  // Log the error at the CALLER's location - which is why the macro passes
  // __FILE__/__LINE__/__func__ in rather than letting the trace macro capture
  // this file. A ProblemDetails that never reaches the log is a 400 nobody can
  // explain afterwards.
  //
  // ktOut() rather than KT_E(): KT_E captures __FILE__ and __LINE__ at ITS OWN
  // call site, which would point every error in the broker at this line.
  //
  ktOut(fileName, lineNo, functionName, 'E', -1, "%d %s: %s", status, title, corRest.out.problemDetail);
}



// -----------------------------------------------------------------------------
//
// ldErrorExtraString -
//
void ldErrorExtraString(const char* name, const char* value)
{
  if (value == NULL)
    return;

  if (corRest.out.problemExtras == NULL)
    corRest.out.problemExtras = kjObject(corRest.kjsonP, NULL);

  kjChildAdd(corRest.out.problemExtras, kjString(corRest.kjsonP, name, value));
}



// -----------------------------------------------------------------------------
//
// ldErrorExtraInt -
//
void ldErrorExtraInt(const char* name, int value)
{
  if (corRest.out.problemExtras == NULL)
    corRest.out.problemExtras = kjObject(corRest.kjsonP, NULL);

  kjChildAdd(corRest.out.problemExtras, kjInteger(corRest.kjsonP, name, value));
}



// -----------------------------------------------------------------------------
//
// ldGeoTypeConflict -
//
void ldGeoTypeConflict(void)
{
  const char* attrName = (corNgsild.geoConflictAttr != NULL) ? corNgsild.geoConflictAttr : "the Attribute";

  //
  // § 5.2.6.4 ties no Attribute name to a single type — the same name may be a
  // GeoProperty on one Entity and a Property on another, and ramdb stores exactly
  // that. It is the mongoc backend that cannot: its 2dsphere index is created per
  // Attribute PATH and applies to the whole collection, i.e. to every Entity of
  // the tenant, so one name cannot hold a geometry and a non-geometry at once.
  //
  // Conflict rather than BadRequestData: the payload is well-formed and would be
  // accepted against an empty tenant. What refuses it is the state of the tenant.
  //
  ldError(409, LD_ERROR_CONFLICT, "Attribute Type Conflict",
          "attribute '%s' is already in use with a conflicting Attribute type in this tenant "
          "(a GeoProperty and another type cannot share one Attribute name here)", attrName);

  ldErrorExtraString("attributeName", corNgsild.geoConflictAttr);
}
