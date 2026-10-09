#ifndef CORNGSILD_LDPAGINATION_H_
#define CORNGSILD_LDPAGINATION_H_

//
// FILE            ldPagination.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include <stdint.h>                                      // uint64_t
#include <stdbool.h>                                     // bool

#include "corTree/CorNode.h"                            // CorNode
#include "corNgsild/LdEntityMap.h"                       // LdEntityMap



// -----------------------------------------------------------------------------
//
// ldPaginationTrim - trim CorNode array to limit, return true if there were more
//
extern bool ldPaginationTrim(CorNode* arrayP, int limit);



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
// ldPaginationLinkHeaderAt - as ldPaginationLinkHeader, with the next page starting at nextOffset
//                            (a page shorter than limit: offset + the entities it holds)
//
extern void ldPaginationLinkHeaderAt(bool hasMore, int nextOffset);



// -----------------------------------------------------------------------------
//
// ldPaginationSeekLinkHeader - the Link header of a page that is a position in the result's order
//
// next / prev carry the request's parameters (but limit, offset and skipMask's), limit, and
// <nextParam>=<nextCursor> / <prevParam>=<prevCursor> - a NULL cursor: no such link.
//
extern void ldPaginationSeekLinkHeader(uint64_t skipMask, const char* nextParam, const char* nextCursor, const char* prevParam, const char* prevCursor);



// -----------------------------------------------------------------------------
//
// ldPaginationEntityMapLinkHeader - the Link header of a page served from an EntityMap: every link
//                                   names the map (entityMap=<mapId>) and repeats the query (§ 9.6)
//
extern void ldPaginationEntityMapLinkHeader(LdEntityMap* mapP, int offset, int limit, int nextOffset, int total, bool hasMore);



// -----------------------------------------------------------------------------
//
// ldTemporalPaginationLinkHeader - Link rel="intervalafter"/"intervalbefore"
//                                  page pointers (§ 6.4.7.3)
//
extern void ldTemporalPaginationLinkHeader(bool hasMore, int pageLimit);

#endif  // CORNGSILD_LDPAGINATION_H_
