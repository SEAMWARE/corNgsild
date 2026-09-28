//
// FILE            ldStripSysAttrs.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include <stdbool.h>                                     // bool
#include <string.h>                                    // strcmp

#include "corTree/CorNode.h"                          // CorNode
#include "corTree/corTreeBuilder.h"                   // corTreeChildRemove
#include "corNgsild/ldTermClass.h"                    // ldTermClass, LD_TC_*
#include "corNgsild/ldTermId.h"                       // ldTermId
#include "corNgsild/LdVocab.h"                          // LD_VOCAB_*

#include "corNgsild/ldStripSysAttrs.h"                  // Own interface



// -----------------------------------------------------------------------------
//
// isSysAttr -
//
static bool isSysAttr(CorNode* nodeP)
{
  return (ldTermClass[ldTermId(nodeP)] & LD_TC_SYS_ATTR) != 0;
}



// -----------------------------------------------------------------------------
//
// isValueKey - value keys should NOT be recursed into
//
static bool isValueKey(CorNode* nodeP)
{
  return (ldTermClass[ldTermId(nodeP)] & LD_TC_VALUE_KEY) != 0;
}



// -----------------------------------------------------------------------------
//
// stripObject - remove createdAt/modifiedAt, recurse into sub-attributes only
//
static void stripObject(CorNode* objectP)
{
  if (objectP == NULL || objectP->type != CorObject)
    return;

  CorNode* childP = objectP->value.head;

  while (childP != NULL)
  {
    CorNode* nextP = childP->next;

    if (childP->name != NULL && isSysAttr(childP))
      corTreeChildRemove(objectP, childP);
    else if (childP->type == CorObject && childP->name != NULL && !isValueKey(childP))
      stripObject(childP);
    else if (childP->type == CorArray)
    {
      for (CorNode* itemP = childP->value.head; itemP != NULL; itemP = itemP->next)
        stripObject(itemP);
    }

    childP = nextP;
  }
}



// -----------------------------------------------------------------------------
//
// ldStripSysAttrs -
//
void ldStripSysAttrs(CorNode* treeP)
{
  if (treeP == NULL)
    return;

  if (treeP->type == CorObject)
  {
    stripObject(treeP);
  }
  else if (treeP->type == CorArray)
  {
    for (CorNode* itemP = treeP->value.head; itemP != NULL; itemP = itemP->next)
      stripObject(itemP);
  }
}
