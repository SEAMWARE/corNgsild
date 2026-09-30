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
#include "corNgsild/ldQExpandValues.h"                   // ldAttrListExpand
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

  // Render the pre-parsed tree with compaction against the response @context - the values
  // expandValues named, stored expanded, compacted back as well
  CorNode* evP = corTreeLookup(subP, "expandValues");
  char**   evV = ((evP != NULL) && (evP->type == CorString)) ? ldAttrListExpand(evP->value.s, contextP, allocP) : NULL;
  char*    compactedQ = ldQRenderCompactValues(qExpr, contextP, allocP, true, evV);
  if (compactedQ != NULL)
    qP->value.s = compactedQ;
}
