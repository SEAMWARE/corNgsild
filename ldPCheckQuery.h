//
// FILE            ldPCheckQuery.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef CORNGSILD_LD_PCHECK_QUERY_H_
#define CORNGSILD_LD_PCHECK_QUERY_H_

#include <stdbool.h>                                     // bool



// -----------------------------------------------------------------------------
//
// pCheckQuery - validate the POST /entityOperations/query payload body
//
// Registered as the 'payloadCheck' hook for the query route, so it runs on the
// parsed requestTree BEFORE the service routine. A malformed Query object is
// rejected with a pinpointed 400 BadRequestData (wrong JSON type, duplicated /
// unknown / empty field, bad entities/attrs member, unparseable q) instead of
// being half-ignored by the handler. Returns false (and sets the ProblemDetails
// via ldError) on the first violation; true if the body is a valid Query.
//
extern bool pCheckQuery(void);

#endif  // CORNGSILD_LD_PCHECK_QUERY_H_
