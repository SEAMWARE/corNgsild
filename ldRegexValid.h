#ifndef CORNGSILD_LDREGEXVALID_H_
#define CORNGSILD_LDREGEXVALID_H_

//
// FILE            ldRegexValid.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                  // bool



// -----------------------------------------------------------------------------
//
// ldRegexValid - is 'pattern' a valid regular expression "as per IEEE 1003.2" (an idPattern, TS 104 175
// § 5.2.8 / § 5.2.9)? An extended regular expression that regcomp compiles, and none of what POSIX leaves
// UNDEFINED (XBD 9.4.6): a duplication symbol (*, +, ?, an interval) right after another one, or with
// nothing to repeat - at the start, after '(' or '|'. glibc accepts those; a PCRE engine (MongoDB's)
// refuses them - the answer must not depend on the database.
//
// False: 'errBuf' says why.
//
extern bool ldRegexValid(const char* pattern, char* errBuf, int errBufLen);

#endif  // CORNGSILD_LDREGEXVALID_H_
