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
// ldPaginationMediaType - the Link "type" attribute for pagination links
//                         (§ 6.4.7.2: the original request's media type)
//
extern const char* ldPaginationMediaType(void);



// -----------------------------------------------------------------------------
//
// ldPaginationLinkHeader - add Link header with next/prev pagination links
//
extern void ldPaginationLinkHeader(bool hasMore);



// -----------------------------------------------------------------------------
//
// ldTemporalPaginationLinkHeader - Link rel="intervalafter"/"intervalbefore"
//                                  page pointers (§ 6.4.7.3)
//
extern void ldTemporalPaginationLinkHeader(bool hasMore, int pageLimit);

#endif  // SWNGSILD_LDPAGINATION_H_
