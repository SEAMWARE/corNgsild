#ifndef CORNGSILD_LDQRENDER_H_
#define CORNGSILD_LDQRENDER_H_

//
// FILE            ldQRender.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Render a parsed LdQNode tree back to a q-filter string.
// Used to reconstruct the q-string for subscription GET responses,
// with attribute names compacted against the response @context.
//
#include <stdbool.h>                                   // bool
#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                              // KjNode (unused but conventional)
#include "corJsonld/CorLdContext.h"                      // CorLdContext

#include "corNgsild/LdQ.h"                              // LdQNode



// -----------------------------------------------------------------------------
//
// ldQRender - render an LdQNode tree to a q-filter string
//
// contextP:      @context to compact attribute IRIs (NULL = no compaction)
// allocP:        allocator for the output string (NULL = malloc)
// qGrammarOnly:  true when rendering into a response BODY — encode only the
//                q-grammar-significant chars of an uncompactable IRI, leaving
//                URL-reserved chars raw. false for a forward URL (full encoding).
//
extern char* ldQRender(LdQNode* nodeP, CorLdContext* contextP, KAlloc* allocP, bool qGrammarOnly);



// -----------------------------------------------------------------------------
//
// ldCompactOrEncode - compact an IRI against the @context, URL-encode if uncompactable
//
// For names embedded in URLs (q-filter terms, pick= lists, /attrs/{attrId}
// path segments): the receiver interprets them via the @context the request
// carries, so compact against THAT context; when the IRI has no short form
// there, %-encode it so the syntax survives. qGrammarOnly=true encodes only the
// q-grammar chars (response body); false encodes everything but RFC 3986
// unreserved (forward URL). NULL contextP returns the IRI untouched (storage).
//
extern const char* ldCompactOrEncode(const char* iri, CorLdContext* contextP, KAlloc* allocP, bool qGrammarOnly);

#endif  // CORNGSILD_LDQRENDER_H_
