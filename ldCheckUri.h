#ifndef CORNGSILD_LDCHECKURI_H_
#define CORNGSILD_LDCHECKURI_H_

//
// FILE            ldCheckUri.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include <stdbool.h>                                     // bool



// -----------------------------------------------------------------------------
//
// ldUriValid - is this a URI? (the predicate, with no ProblemDetails)
//
// A scheme starting with a letter, a colon, something after it, and no
// whitespace. Callers that have their own error to raise - the q parser
// answers "Invalid q parameter", not "Invalid URI" - use this one.
//
extern bool ldUriValid(const char* uri);



// -----------------------------------------------------------------------------
//
// ldCheckUri - ldUriValid, raising 400 "Invalid URI" when it is not
//
extern bool ldCheckUri(const char* uri);

#endif  // CORNGSILD_LDCHECKURI_H_
