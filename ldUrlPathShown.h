#ifndef CORNGSILD_LDURLPATHSHOWN_H_
#define CORNGSILD_LDURLPATHSHOWN_H_

//
// FILE            ldUrlPathShown.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//



// -----------------------------------------------------------------------------
//
// ldUrlPathShown - a URL up to its query ('?'), its password as "***" (corUriPasswordMask), into buf - for a
// trace of a notification or a forwarded request, whose query parameters are traced one by one. Returns buf.
//
extern char* ldUrlPathShown(const char* url, char* buf, int bufSize);

#endif  // CORNGSILD_LDURLPATHSHOWN_H_
