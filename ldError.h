//
// FILE            ldError.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#ifndef SWNGSILD_LDERROR_H_
#define SWNGSILD_LDERROR_H_



// -----------------------------------------------------------------------------
//
// ldErrorFunction - implementation (use the ldError macro instead)
//
extern void ldErrorFunction
(
  int          status,
  const char*  type,
  const char*  title,
  const char*  fileName,
  int          lineNo,
  const char*  functionName,
  const char*  fmt,
  ...
);



// -----------------------------------------------------------------------------
//
// ldError - set problem details in swRest.out and log the error
//
// Macro captures caller's __FILE__, __LINE__, __FUNCTION__ so that the log
// line points to the place where the error was detected, not to ldError.c.
//
#define ldError(status, type, title, ...)  \
  ldErrorFunction(status, type, title, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)



// -----------------------------------------------------------------------------
//
// ldErrorExtraString - attach an RFC 9457 extension member to the ProblemDetails
//
// Adds a { name: value } member to swRest.out.problemExtras (created on first
// use), spliced into the error body alongside type/title/status/detail. Use for
// machine-readable context — e.g. the registrationId of a failed forward — so
// clients need not parse it out of the English 'detail'. No-op if value is NULL.
//
extern void ldErrorExtraString(const char* name, const char* value);



// -----------------------------------------------------------------------------
//
// ldErrorExtraInt - attach an integer RFC 9457 extension member (e.g. an upstream statusCode)
//
extern void ldErrorExtraInt(const char* name, int value);

#endif  // SWNGSILD_LDERROR_H_
