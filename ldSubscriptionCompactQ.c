//
// FILE            ldSubscriptionCompactQ.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <string.h>                                    // strcmp

#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corTree/CorNode.h"                           // CorNode
#include "corTree/corTreeLookup.h"                     // corTreeLookup

#include "corNgsild/LdQ.h"                              // LdQNode
#include "corNgsild/ldQRender.h"                        // ldQRender
#include "corNgsild/ldSubscriptionCompactQ.h"           // Own interface



// -----------------------------------------------------------------------------
//
// ldSubscriptionCompactQ -
//
void ldSubscriptionCompactQ(CorNode* subP, LdQNode* qExpr, CorLdContext* contextP, CorAlloc* allocP)
{
  if (qExpr == NULL)
    return;

  CorNode* qP = corTreeLookup(subP, "q");

  if (qP == NULL || qP->type != CorString)
    return;

  // Render the pre-parsed tree with compaction against the response @context
  char* compactedQ = ldQRender(qExpr, contextP, allocP, true);
  if (compactedQ != NULL)
    qP->value.s = compactedQ;
}
