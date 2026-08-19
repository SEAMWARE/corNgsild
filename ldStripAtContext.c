//
// FILE            ldStripAtContext.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//

#include <string.h>                                       // strcmp

#include "kjson/KjNode.h"                                 // KjNode
#include "kjson/kjBuilder.h"                              // kjChildRemove

#include "corNgsild/ldStripAtContext.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// ldStripAtContext -
//
void ldStripAtContext(KjNode* treeP)
{
  if (treeP == NULL)
    return;

  if (treeP->type == KjObject)
  {
    KjNode* childP = treeP->value.firstChildP;
    while (childP != NULL)
    {
      KjNode* nextP = childP->next;
      if (childP->name != NULL && strcmp(childP->name, "@context") == 0)
        kjChildRemove(treeP, childP);
      else
        ldStripAtContext(childP);
      childP = nextP;
    }
  }
  else if (treeP->type == KjArray)
  {
    for (KjNode* childP = treeP->value.firstChildP; childP != NULL; childP = childP->next)
      ldStripAtContext(childP);
  }
}
