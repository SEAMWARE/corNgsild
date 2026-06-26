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
#include "swNgsild/ldParams.h"                           // LD_PARAM_LIMIT, LD_PARAM_OFFSET
#include "swNgsild/ldAcceptParse.h"                      // ldAcceptParse, LdAcceptType

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
// ldPaginationMediaType - the Link "type" Target Attribute for pagination links
//
// § 6.4.7.2 (TS 104-176): the "type" attribute shall be exactly the media type
// of the ORIGINAL request (its Accept header), carried through the whole
// pagination iteration. Per RFC 8288 § 3.4.1 it is only a non-binding hint —
// it does not override the Content-Type a client gets by following the link —
// but the spec still mandates it mirror the request, not a fixed value. The
// render hook sets swRest.out.contentType after the service routine runs, so
// evaluate Accept here the same way the render hook will.
//
const char* ldPaginationMediaType(void)
{
  switch (ldAcceptParse(swRest.in.accept))
  {
  case LdAcceptGeoJson: return "application/geo+json";
  case LdAcceptLdJson:  return "application/ld+json";
  default:              return "application/json";
  }
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

  // No header needed if this is the first page and there are no more results
  if (!hasMore && offset == 0)
    return;

  // Build the query string from original URI params, skipping limit /
  // offset (we'll add our own pagination values).
  char params[2048];
  params[0] = '\0';
  int  pLen = 0;

  for (int i = 0; i < swRest.in.uriParamCount; i++)
  {
    // Skip the pagination params we re-emit ourselves (tested by bit, not name).
    if (swRest.in.uriParamV[i].bit & (LD_PARAM_LIMIT | LD_PARAM_OFFSET))
      continue;

    pLen += snprintf(params + pLen, sizeof(params) - pLen, "%s=%s&",
                     swRest.in.uriParamV[i].key, swRest.in.uriParamV[i].value);
  }

  // Build Link header value
  // Max: two link-values, each ~300 bytes => 1024 is plenty
  int  bufSize = 1024;
  char* buf = (char*) kaAlloc(&swRest.kalloc, bufSize);
  int  bLen = 0;

  const char* mediaType = ldPaginationMediaType();

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

    bLen += snprintf(buf + bLen, bufSize - bLen, "<%s?%slimit=%d&offset=%d>;rel=\"prev\";type=\"%s\"",
                     swRest.in.urlPath, params, limit, prevOffset, mediaType);
  }

  // next link (when more results exist)
  if (hasMore)
  {
    int nextOffset = offset + limit;

    if (bLen > 0)
      bLen += snprintf(buf + bLen, bufSize - bLen, ", ");

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



// -----------------------------------------------------------------------------
//
// ldTemporalPaginationLinkHeader - Link header with intervalafter/intervalbefore
//
// § 6.4.7.3 (TS 104-176): temporal pagination page pointers. The next page
// (ascending order of Attribute timestamps) is rel="intervalafter", the
// previous one rel="intervalbefore". Realized as transparent offsetN-based
// pagination: the URI-references repeat the original query parameters with
// offsetN moved by the effective per-attribute page limit.
//
// pageLimit: the effective per-attribute limit (firstN / lastN / the
// implementation default — the plugin reports it via TroeRangeInfo.size).
// hasMore:   instances remain beyond the current page in the pagination
//            direction.
//
void ldTemporalPaginationLinkHeader(bool hasMore, int pageLimit)
{
  int offsetN = swNgsild.offsetN;

  // First page and nothing beyond it — no pointers needed.
  if (!hasMore && offsetN == 0)
    return;

  if (pageLimit <= 0)
    return;

  // Rebuild the query string from the original URI params, skipping
  // offsetN (we add our own value per pointer).
  char params[2048];
  int  pLen = 0;
  params[0] = '\0';

  for (int i = 0; i < swRest.in.uriParamCount; i++)
  {
    const char* name = swRest.in.uriParamV[i].key;

    if (strcmp(name, "offsetN") == 0)
      continue;

    pLen += snprintf(params + pLen, sizeof(params) - pLen, "%s=%s&", name, swRest.in.uriParamV[i].value);
  }

  const char* mediaType = ldPaginationMediaType();

  // § 6.4.7.3 names the relations by TIME, not by iteration:
  // "intervalafter" points at the page with LATER Attribute timestamps,
  // "intervalbefore" at the page with EARLIER ones. Paginating
  // descending (?lastN), a deeper page (larger offsetN) holds earlier
  // timestamps — the relations swap sides.
  bool        descending = (swNgsild.lastN > 0);
  const char* deeperRel  = descending ? "intervalbefore" : "intervalafter";
  const char* shallowRel = descending ? "intervalafter"  : "intervalbefore";

  int   bufSize = 1024 + 2 * (pLen + (int) strlen(swRest.in.urlPath));
  char* buf     = (char*) kaAlloc(&swRest.kalloc, bufSize);
  int   bLen    = 0;

  // Shallower page (towards offsetN=0); absent on the first page.
  if (offsetN > 0)
  {
    int prevOffset = offsetN - pageLimit;
    if (prevOffset < 0)
      prevOffset = 0;

    bLen += snprintf(buf + bLen, bufSize - bLen, "<%s?%soffsetN=%d>;rel=\"%s\";type=\"%s\"",
                     swRest.in.urlPath, params, prevOffset, shallowRel, mediaType);
  }

  // Deeper page, when more instances exist in the pagination direction.
  if (hasMore)
  {
    if (bLen > 0)
      bLen += snprintf(buf + bLen, bufSize - bLen, ", ");

    bLen += snprintf(buf + bLen, bufSize - bLen, "<%s?%soffsetN=%d>;rel=\"%s\";type=\"%s\"",
                     swRest.in.urlPath, params, offsetN + pageLimit, deeperRel, mediaType);
  }

  if (swRest.out.headerCount < swRest.out.headerSize)
  {
    swRest.out.headerV[swRest.out.headerCount].key   = (char*) "Link";
    swRest.out.headerV[swRest.out.headerCount].value = buf;
    swRest.out.headerCount++;
  }
}
