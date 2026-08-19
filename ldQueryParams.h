#ifndef CORNGSILD_LDQUERYPARAMS_H_
#define CORNGSILD_LDQUERYPARAMS_H_

//
// FILE            ldQueryParams.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include "kalloc/KAlloc.h"                         // KAlloc



// -----------------------------------------------------------------------------
//
// ldQueryParamValue - look up a URL parameter value by name
//
extern char* ldQueryParamValue(const char* name);



// -----------------------------------------------------------------------------
//
// ldParamSplit - split a comma-separated value into a NULL-terminated array
//
// Returns a faP-allocated array of pointers into the original (modified) string.
// The input string is modified in place: commas are replaced with '\0'.
//
extern char** ldParamSplit(char* csv, KAlloc* faP);



// -----------------------------------------------------------------------------
//
// ldParamExpandV - expand each value in a NULL-terminated string array
//
// Returns a new faP-allocated array with expanded URIs.
//
extern char** ldParamExpandV(char** srcV, KAlloc* faP);

#endif  // CORNGSILD_LDQUERYPARAMS_H_
