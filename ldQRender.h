#ifndef SWNGSILD_LDQRENDER_H_
#define SWNGSILD_LDQRENDER_H_

//
// FILE            ldQRender.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Render a parsed LdQNode tree back to a q-filter string.
// Used to reconstruct the q-string for subscription GET responses,
// with attribute names compacted against the response @context.
//
#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                              // KjNode (unused but conventional)
#include "swJsonld/SwldContext.h"                      // SwldContext

#include "swNgsild/LdQ.h"                              // LdQNode



// -----------------------------------------------------------------------------
//
// ldQRender - render an LdQNode tree to a q-filter string
//
// contextP:  @context to compact attribute IRIs (NULL = no compaction, URL-encode)
// allocP:    allocator for the output string (NULL = malloc)
//
extern char* ldQRender(LdQNode* nodeP, SwldContext* contextP, KAlloc* allocP);



// -----------------------------------------------------------------------------
//
// ldCompactOrEncode - compact an IRI against the @context, URL-encode if uncompactable
//
// For names embedded in URLs (q-filter terms, pick= lists, /attrs/{attrId}
// path segments): the receiver interprets them via the @context the request
// carries, so compact against THAT context; when the IRI has no short form
// there, %-encode it (everything but RFC 3986 unreserved) so URL syntax
// survives. NULL contextP returns the IRI untouched (internal storage mode).
//
extern const char* ldCompactOrEncode(const char* iri, SwldContext* contextP, KAlloc* allocP);

#endif  // SWNGSILD_LDQRENDER_H_
