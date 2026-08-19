//
// FILE            ldSubscriptionCompactQ.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <string.h>                                    // strcmp

#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjLookup.h"                            // kjLookup

#include "corNgsild/LdQ.h"                              // LdQNode
#include "corNgsild/ldQRender.h"                        // ldQRender
#include "corNgsild/ldSubscriptionCompactQ.h"           // Own interface



// -----------------------------------------------------------------------------
//
// ldSubscriptionCompactQ -
//
void ldSubscriptionCompactQ(KjNode* subP, LdQNode* qExpr, CorLdContext* contextP, KAlloc* allocP)
{
  if (qExpr == NULL)
    return;

  KjNode* qP = kjLookup(subP, "q");

  if (qP == NULL || qP->type != KjString)
    return;

  // Render the pre-parsed tree with compaction against the response @context
  char* compactedQ = ldQRender(qExpr, contextP, allocP, true);
  if (compactedQ != NULL)
    qP->value.s = compactedQ;
}
