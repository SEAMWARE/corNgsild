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
#include "corTree/corTreeBuilder.h"               // corTreeChildRemove
#include "corNgsild/LdVocab.h"                          // LD_VOCAB_*

#include "corNgsild/ldStripSysAttrs.h"                  // Own interface



// -----------------------------------------------------------------------------
//
// isSysAttr -
//
static bool isSysAttr(const char* name)
{
  if (strcmp(name, LD_VOCAB_CREATED_AT)  == 0)  return true;
  if (strcmp(name, LD_VOCAB_MODIFIED_AT) == 0)  return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// isValueKey - value keys should NOT be recursed into
//
static bool isValueKey(const char* name)
{
  if (strcmp(name, LD_VOCAB_HAS_VALUE)        == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_OBJECT)       == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_LANGUAGE_MAP) == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_VOCAB)        == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_VALUE_LIST)   == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_OBJECT_LIST)  == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_JSON)         == 0)  return true;
  return false;
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

    if (childP->name != NULL && isSysAttr(childP->name))
      corTreeChildRemove(objectP, childP);
    else if (childP->type == CorObject && childP->name != NULL && !isValueKey(childP->name))
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
