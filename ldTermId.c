//
// FILE            ldTermId.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdint.h>                                      // uint16_t

#include "corBase/corLibLog.h"                          // COR_LIB_E
#include "corTree/CorNode.h"                            // CorNode
#include "corJsonld/CorLdItem.h"                        // CorLdItem
#include "corJsonld/corLdCoreLookup.h"                  // corLdCoreLookup

#include "corNgsild/CorTerm.h"                          // CorTerm
#include "corNgsild/ldTermId.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// ldTermIdLookup -
//
// In a DEBUG build every call lands here, and a node that carries an id already has it
// CHECKED against a fresh lookup of its current name: an id stamped under one name and
// read under another (a rename that bypassed ldNodeRename) is exactly the silent
// misclassification this whole mechanism exists to prevent.
//
CorTerm ldTermIdLookup(CorNode* nodeP)
{
  if (nodeP->name == NULL)
    return CorTermNone;

  CorLdItem* itemP  = corLdCoreLookup(nodeP->name);
  uint16_t   fresh  = ((itemP != NULL) && (itemP->termId != 0)) ? itemP->termId : LD_TERM_NOT_CORE;

#ifdef DEBUG
  if ((nodeP->termId != 0) && (nodeP->termId != fresh))
    COR_LIB_E("stale term id on node '%s': %d, its name says %d", nodeP->name, nodeP->termId, fresh);
#endif

  nodeP->termId = fresh;

  return (fresh == LD_TERM_NOT_CORE) ? CorTermNone : (CorTerm) fresh;
}
