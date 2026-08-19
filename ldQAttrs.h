#ifndef CORNGSILD_LDQATTRS_H_
#define CORNGSILD_LDQATTRS_H_

//
// FILE            ldQAttrs.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Collect every attribute name referenced anywhere in an LdQNode tree.
// Both LdQTermNode (term.attr) and LdQLinkedNode (linked.relName +
// recursive walk into linked.subQ) contribute. Used by the dist-op
// forwarder to compute the set of attrs that must be present on the
// remote-returned slice so a local re-evaluation of q stays sound.
//
#include "kalloc/KAlloc.h"                             // KAlloc

#include "corNgsild/LdQ.h"                              // LdQNode



// -----------------------------------------------------------------------------
//
// ldQAttrs - build a NULL-terminated array of attr IRIs referenced by node
//
// nodeP   : root of the q-expression tree (NULL → returns NULL)
// kaP     : allocator for the output array (the attr strings themselves
//           are borrowed pointers into the LdQNode tree, no copy)
//
// Returns a NULL-terminated char** allocated in kaP, or NULL if the tree
// is empty / no terms.  Duplicates are dropped (the same attr referenced
// multiple times appears once).
//
extern char** ldQAttrs(LdQNode* nodeP, KAlloc* kaP);

#endif  // CORNGSILD_LDQATTRS_H_
