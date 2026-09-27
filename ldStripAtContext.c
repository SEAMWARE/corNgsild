//
// FILE            ldStripAtContext.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//

#include <string.h>                                       // strcmp

#include "corTree/CorNode.h"                              // CorNode
#include "corTree/corTreeBuilder.h"                       // corTreeChildRemove

#include "corNgsild/ldStripAtContext.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// ldStripAtContext -
//
void ldStripAtContext(CorNode* treeP)
{
  if (treeP == NULL)
    return;

  if (treeP->type == CorObject)
  {
    CorNode* childP = treeP->value.head;
    while (childP != NULL)
    {
      CorNode* nextP = childP->next;
      if (childP->name != NULL && strcmp(childP->name, "@context") == 0)
        corTreeChildRemove(treeP, childP);
      else
        ldStripAtContext(childP);
      childP = nextP;
    }
  }
  else if (treeP->type == CorArray)
  {
    for (CorNode* childP = treeP->value.head; childP != NULL; childP = childP->next)
      ldStripAtContext(childP);
  }
}
