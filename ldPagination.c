//
// FILE            ldPagination.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdio.h>                                       // snprintf
#include <string.h>                                      // strcmp, strlen

#include "kalloc/KAlloc.h"                             // kaAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "swRest/swRest.h"                             // swRest
#include "swNgsild/SwNgsild.h"                           // swNgsild

#include "swNgsild/ldPagination.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// ldPaginationTrim - trim KjNode array to limit, return true if there were more
//
// If the array has more than 'limit' children, unlink the last one and return true.
// This implements the "limit+1" strategy: fetch limit+1 from DB, trim to limit,
// and use the return value to know if more results exist.
//
bool ldPaginationTrim(KjNode* arrayP, int limit)
{
  if (arrayP == NULL || limit <= 0)
    return false;

  // Count children and find the node before the last one
  int      count = 0;
  KjNode*  prevP = NULL;
  KjNode*  nodeP = arrayP->value.firstChildP;

  while (nodeP != NULL)
  {
    count++;
    if (count > limit)
    {
      // Unlink this node (it's the limit+1'th)
      if (prevP != NULL)
        prevP->next = NULL;
      arrayP->lastChild = prevP;
      return true;
    }
    prevP = nodeP;
    nodeP = nodeP->next;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// ldPaginationLinkHeader - add Link header with next/prev pagination links
//
// Builds a Link header (RFC 8288) with rel="next" and/or rel="prev" based on
// the current offset and whether more results exist beyond the current page.
//
void ldPaginationLinkHeader(bool hasMore)
{
  int offset = swNgsild.offset;
  int limit  = swNgsild.limit;
  // If the request used `?page=<N>` instead of `?offset=`, mirror that
  // in the Link header values (compat with clients that issued page-
  // style pagination).
  bool usePage = (swNgsild.page > 0);

  // No header needed if this is the first page and there are no more results
  if (!hasMore && offset == 0)
    return;

  // Build the query string from original URI params, skipping limit /
  // offset / page (we'll add our own pagination values).
  char params[2048];
  params[0] = '\0';
  int  pLen = 0;

  for (int i = 0; i < swRest.in.uriParamCount; i++)
  {
    const char* name = swRest.in.uriParamV[i].key;

    if (strcmp(name, "limit") == 0 || strcmp(name, "offset") == 0 || strcmp(name, "page") == 0)
      continue;

    pLen += snprintf(params + pLen, sizeof(params) - pLen, "%s=%s&", name, swRest.in.uriParamV[i].value);
  }

  // Build Link header value
  // Max: two link-values, each ~300 bytes => 1024 is plenty
  int  bufSize = 1024;
  char* buf = (char*) kaAlloc(&swRest.kalloc, bufSize);
  int  bLen = 0;

  // The Link "type" attribute advertises the media type of the
  // referenced resource — not the current response's Content-Type.
  // NGSI-LD resources are intrinsically JSON-LD (the application/
  // json variant is just the same payload with @context stripped
  // out of the body and into a Link header). So pagination Links
  // always advertise application/ld+json — the canonical shape —
  // matching the conformance suite's expectation regardless of
  // which Accept variant the client requested.
  const char* mediaType = "application/ld+json";

  // prev link only (rel=first / rel=last are permitted by § 6.3.10
  // but redundant for offset/limit clients and the ETSI conformance
  // suite only checks prev + next — emitting "first" produces an
  // extra comma-separated entry that the suite's split-and-compare
  // assertion counts as a spurious link).
  if (offset > 0)
  {
    int prevOffset = offset - limit;
    if (prevOffset < 0)
      prevOffset = 0;
    int prevPage = (limit > 0) ? (prevOffset / limit) + 1 : 1;

    if (usePage)
      bLen += snprintf(buf + bLen, bufSize - bLen, "<%s?%slimit=%d&page=%d>;rel=\"prev\";type=\"%s\"",
                       swRest.in.urlPath, params, limit, prevPage, mediaType);
    else
      bLen += snprintf(buf + bLen, bufSize - bLen, "<%s?%slimit=%d&offset=%d>;rel=\"prev\";type=\"%s\"",
                       swRest.in.urlPath, params, limit, prevOffset, mediaType);
  }

  // next link (when more results exist)
  if (hasMore)
  {
    int nextOffset = offset + limit;
    int nextPage   = (limit > 0) ? (nextOffset / limit) + 1 : 1;

    if (bLen > 0)
      bLen += snprintf(buf + bLen, bufSize - bLen, ", ");

    if (usePage)
      bLen += snprintf(buf + bLen, bufSize - bLen, "<%s?%slimit=%d&page=%d>;rel=\"next\";type=\"%s\"",
                       swRest.in.urlPath, params, limit, nextPage, mediaType);
    else
      bLen += snprintf(buf + bLen, bufSize - bLen, "<%s?%slimit=%d&offset=%d>;rel=\"next\";type=\"%s\"",
                       swRest.in.urlPath, params, limit, nextOffset, mediaType);
  }

  // Add Link header to response
  if (swRest.out.headerCount < swRest.out.headerSize)
  {
    swRest.out.headerV[swRest.out.headerCount].key   = (char*) "Link";
    swRest.out.headerV[swRest.out.headerCount].value  = buf;
    swRest.out.headerCount++;
  }
}
