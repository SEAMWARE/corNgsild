#ifndef CORNGSILD_LD_TERM_ID_H_
#define CORNGSILD_LD_TERM_ID_H_

//
// FILE            ldTermId.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdint.h>                                      // uint16_t

#include "corTree/CorNode.h"                            // CorNode
#include "corNgsild/CorTerm.h"                          // CorTerm



// -----------------------------------------------------------------------------
//
// CorNode.termId, as corNgsild uses it
//
//   0                   not known yet - nobody has looked
//   1 .. CorTermLast-1  the core term the node's NAME is (a CorTerm)
//   LD_TERM_NOT_CORE    looked up: not a core term (a user term, a value key, ...)
//
// The top bit is free by construction (CorTerm ids fit in 15 bits - see CorTerm.h), so
// "not core" is remembered too and a user attribute costs one lookup, like a core one.
//
#define LD_TERM_NOT_CORE  0x8000



// -----------------------------------------------------------------------------
//
// ldTermIdLookup - the slow path of ldTermId: one corLdCoreLookup, cached on the node
//
extern CorTerm ldTermIdLookup(CorNode* nodeP);



// -----------------------------------------------------------------------------
//
// ldTermId - which core term is this node's NAME? CorTermNone if none
//
// The ONLY way NGSI-LD code reads a node's term. A node that came through the parser
// or the expander is stamped already and costs a load; one read from a database or
// built by hand costs one hash probe, the first time only.
//
// Says nothing about the node's ROLE - an "observedAt" inside a compound value is the
// core term observedAt too. Where the node sits is the caller's business.
//
// ⚠ A node whose name changes must go through ldNodeRename, or its id goes stale (DEBUG
// builds check, and log an error on a stale id).
//
static inline CorTerm ldTermId(CorNode* nodeP)
{
#ifndef DEBUG
  uint16_t id = nodeP->termId;

  if (id == LD_TERM_NOT_CORE)
    return CorTermNone;
  if (id != 0)
    return (CorTerm) id;
#endif

  return ldTermIdLookup(nodeP);
}



// -----------------------------------------------------------------------------
//
// ldNodeRename - change a node's name, and forget its term id
//
static inline void ldNodeRename(CorNode* nodeP, const char* name)
{
  nodeP->name   = (char*) name;
  nodeP->termId = 0;
}

#endif  // CORNGSILD_LD_TERM_ID_H_
