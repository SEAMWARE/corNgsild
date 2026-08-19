#ifndef CORNGSILD_LDSUBSCRIPTIONCOMPACTQ_H_
#define CORNGSILD_LDSUBSCRIPTIONCOMPACTQ_H_

//
// FILE            ldSubscriptionCompactQ.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Compact the q-filter string inside a subscription tree.
// Parses the stored (expanded) q string, compacts attr IRIs against
// the response @context, URL-encodes uncompactable IRIs, and replaces
// the q value in the subscription tree.
//
#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                              // KjNode
#include "corJsonld/CorLdContext.h"                      // CorLdContext
#include "corNgsild/LdQ.h"                              // LdQNode



// -----------------------------------------------------------------------------
//
// ldSubscriptionCompactQ - compact q-filter in a subscription tree for response
//
// subP:      subscription tree (q value will be replaced in-place)
// qExpr:     pre-parsed q-filter tree (from cache). If NULL, q is left as-is.
// contextP:  @context to compact against (NULL = URL-encode all attr IRIs)
// allocP:    allocator for the compacted string
//
extern void ldSubscriptionCompactQ(KjNode* subP, LdQNode* qExpr, CorLdContext* contextP, KAlloc* allocP);

#endif  // CORNGSILD_LDSUBSCRIPTIONCOMPACTQ_H_
