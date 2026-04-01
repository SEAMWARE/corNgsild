#ifndef SWNGSILD_LDPAGINATION_H_
#define SWNGSILD_LDPAGINATION_H_

//
// FILE            ldPagination.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool

#include "kjson/KjNode.h"                               // KjNode



// -----------------------------------------------------------------------------
//
// ldPaginationTrim - trim KjNode array to limit, return true if there were more
//
extern bool ldPaginationTrim(KjNode* arrayP, int limit);



// -----------------------------------------------------------------------------
//
// ldPaginationLinkHeader - add Link header with next/prev pagination links
//
extern void ldPaginationLinkHeader(bool hasMore);

#endif  // SWNGSILD_LDPAGINATION_H_
