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
// ldQParseStored - ldQParse for a q read back from storage (a subscription, from the DB or a cache)
//
// ldQRenderStored wrote it fully resolved: attribute names as IRIs, the values expandValues named
// expanded and quoted, and each [..] segment either an IRI (a member of a compound value, expanded)
// or a raw language tag (under an attribute langProperties named). Parsed again with no context -
// after a restart, on another broker - nothing may be expanded: a raw `es` would become
// https://.../default-context/es and match nothing.
//
extern LdQNode* ldQParseStored(const char* q, CorAlloc* kaP);



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
