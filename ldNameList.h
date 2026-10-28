#ifndef CORNGSILD_LDNAMELIST_H_
#define CORNGSILD_LDNAMELIST_H_

//
// FILE            ldNameList.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                     // bool

#include "corAlloc/CorAlloc.h"                           // CorAlloc
#include "corTree/CorNode.h"                             // CorNode



// -----------------------------------------------------------------------------
//
// ldNameListCheck - a list of names in a payload body: a String or a String[]
//
// expandValues, jsonKeys, langProperties (Query § 5.2.6.5.1, Subscription § 5.2.6.5.2) and
// aggrMethods (TemporalParams § 5.2.6.7.4): a String is a comma separated list of names; in a
// String[] each item is ONE name - never a comma separated list. A name may be short or already
// expanded (a URI); an empty array is refused. Anything else is a 400 BadRequestData, title as
// given, the member named.
//
extern bool ldNameListCheck(CorNode* nodeP, const char* title, const char* member);



// -----------------------------------------------------------------------------
//
// ldNameListCsv - the list as the comma separated string the URL parameter carries
//
// A String: its value. A String[]: its items joined by commas (allocated in kaP). NULL for
// anything else, and for an empty array.
//
extern const char* ldNameListCsv(CorNode* nodeP, CorAlloc* kaP);

#endif  // CORNGSILD_LDNAMELIST_H_
