#ifndef SWNGSILD_LD_STRIP_AT_CONTEXT_H_
#define SWNGSILD_LD_STRIP_AT_CONTEXT_H_

//
// FILE            ldStripAtContext.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//

#include "kjson/KjNode.h"                                 // KjNode



// -----------------------------------------------------------------------------
//
// ldStripAtContext - recursively remove every "@context" child from a tree
//
// Intended for distop response bodies: we parse the peer's reply, then
// strip any @context in it before the tree flows into our handling.
// Request-side @context stripping happens at the HTTP ingress
// (swldExpandTree); this helper is the symmetrical cleanup for the other
// direction — a peer might echo @context inline and we don't want it
// sneaking into our stored or rendered state.
//
// Operates in-place on the given tree. Walks objects and arrays at all
// depths; any child whose name is exactly "@context" is removed.
//
extern void ldStripAtContext(KjNode* treeP);

#endif  // SWNGSILD_LD_STRIP_AT_CONTEXT_H_
