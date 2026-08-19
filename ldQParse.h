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
#include "kalloc/KAlloc.h"                             // KAlloc
#include "corNgsild/LdQ.h"                               // LdQNode



// -----------------------------------------------------------------------------
//
// ldQParse - parse a ?q= expression into an expression tree
//
// Returns a kaP-allocated LdQNode tree, or NULL on parse error (ldError called).
//
extern LdQNode* ldQParse(const char* q, KAlloc* kaP);



// -----------------------------------------------------------------------------
//
// ldQStripLinked - the DB-evaluable "layer 0" of a q expression
//
// Returns a pruned copy of the tree with the § 4.9 linked sub-queries removed
// (treated as "true"/no-constraint), so the storage layer returns an inclusive
// candidate set and the broker's post-filter resolves the linked layers. NULL
// means "no DB-evaluable constraint" (query without q). The input is not mutated.
//
extern LdQNode* ldQStripLinked(LdQNode* node, KAlloc* kaP);

#endif  // CORNGSILD_LDQPARSE_H_
