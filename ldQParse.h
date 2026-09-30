#ifndef CORNGSILD_LDQPARSE_H_
#define CORNGSILD_LDQPARSE_H_

//
// FILE            ldQParse.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corNgsild/LdQ.h"                               // LdQNode



// -----------------------------------------------------------------------------
//
// ldQParse - parse a ?q= expression into an expression tree
//
// Returns a kaP-allocated LdQNode tree, or NULL on parse error (ldError called).
//
extern LdQNode* ldQParse(const char* q, CorAlloc* kaP);



// -----------------------------------------------------------------------------
//
// ldQParseBareWords - ldQParse for an entity QUERY's q, which expandValues may accompany
//
// A bare word (q=category==monument) is not a Value of the grammar, but a query term whose attribute
// is named by expandValues is one to expand with the @context (§ 7.2.3.2 Example 13:
// gender==Male&expandValues=gender) - and the URL parameters arrive in any order, so whether
// expandValues names it is not known here. Such a word is kept as LdQBareWord (== and != only);
// ldExpandParams then expands it, or rejects it with the 400 ldQParse gives. Everything else - a
// subscription's q, csf, ... - uses ldQParse, where a bare word is a 400 at once.
//
extern LdQNode* ldQParseBareWords(const char* q, CorAlloc* kaP);



// -----------------------------------------------------------------------------
//
// ldQStripLinked - the DB-evaluable "layer 0" of a q expression
//
// Returns a pruned copy of the tree with the § 4.9 linked sub-queries removed
// (treated as "true"/no-constraint), so the storage layer returns an inclusive
// candidate set and the broker's post-filter resolves the linked layers. NULL
// means "no DB-evaluable constraint" (query without q). The input is not mutated.
//
extern LdQNode* ldQStripLinked(LdQNode* node, CorAlloc* kaP);

#endif  // CORNGSILD_LDQPARSE_H_
