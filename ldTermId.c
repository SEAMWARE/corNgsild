//
// FILE            ldTermId.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdint.h>                                      // uint16_t
#include <stdlib.h>                                      // abort
#include <string.h>                                      // strchr

#include "corBase/corLibLog.h"                          // COR_LIB_E
#include "corTree/CorNode.h"                            // CorNode
#include "corJsonld/CorLdItem.h"                        // CorLdItem
#include "corJsonld/corLdCoreLookup.h"                  // corLdCoreLookup
#include "corJsonld/corLdExpand.h"                      // contextItemLookup
#include "corJsonld/corLdInit.h"                        // corLdCoreContext

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
  //
  // A DEBUG build takes this path on EVERY call, stamped or not. A stamped node keeps its
  // stamp - exactly what a release build returns - and the stamp must agree with the
  // name: a node renamed without ldNodeRename would be misclassified by a release build
  // only, where no functest looks. So a disagreement stops the broker, loudly: the test
  // that got here fails, and no log has to be read for it to be noticed.
  //
  if (nodeP->termId != 0)
  {
    if (nodeP->termId != fresh)
    {
      COR_LIB_E("stale term id on node '%s': %d, its name says %d - a rename that bypassed ldNodeRename", nodeP->name, nodeP->termId, fresh);
      abort();
    }

    return (nodeP->termId == LD_TERM_NOT_CORE) ? CorTermNone : (CorTerm) nodeP->termId;
  }
#endif

  nodeP->termId = fresh;

  return (fresh == LD_TERM_NOT_CORE) ? CorTermNone : (CorTerm) fresh;
}



// -----------------------------------------------------------------------------
//
// stampCanonical - the term id of one canonical name
//
static uint16_t stampCanonical(const char* name)
{
  CorLdItem* itemP;

  if (name[0] == '@')
    itemP = corLdCoreLookup(name);                       // "@id"/"@type" - the aliases, rare
  else if (strchr(name, ':') != NULL)
    return LD_TERM_NOT_CORE;                             // a user IRI (or a URN key) - core terms are stored short
  else
    itemP = contextItemLookup(corLdCoreContext(), name);

  return ((itemP != NULL) && (itemP->termId != 0)) ? itemP->termId : LD_TERM_NOT_CORE;
}



// -----------------------------------------------------------------------------
//
// ldTreeStampCanonical -
//
void ldTreeStampCanonical(CorNode* treeP)
{
  for (CorNode* nodeP = treeP->value.head; nodeP != NULL; nodeP = nodeP->next)
  {
    if ((nodeP->name != NULL) && (nodeP->termId == 0))
      nodeP->termId = stampCanonical(nodeP->name);

    if ((nodeP->type == CorObject) || (nodeP->type == CorArray))
      ldTreeStampCanonical(nodeP);
  }
}
