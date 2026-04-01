//
// FILE            ldStripSysAttrs.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool
#include <string.h>                                    // strcmp

#include "kjson/KjNode.h"                             // KjNode
#include "kjson/kjBuilder.h"                      // kjChildRemove
#include "swNgsild/LdVocab.h"                          // LD_VOCAB_*

#include "swNgsild/ldStripSysAttrs.h"                  // Own interface



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
static void stripObject(KjNode* objectP)
{
  if (objectP == NULL || objectP->type != KjObject)
    return;

  KjNode* childP = objectP->value.firstChildP;

  while (childP != NULL)
  {
    KjNode* nextP = childP->next;

    if (childP->name != NULL && isSysAttr(childP->name))
      kjChildRemove(objectP, childP);
    else if (childP->type == KjObject && childP->name != NULL && !isValueKey(childP->name))
      stripObject(childP);
    else if (childP->type == KjArray)
    {
      for (KjNode* itemP = childP->value.firstChildP; itemP != NULL; itemP = itemP->next)
        stripObject(itemP);
    }

    childP = nextP;
  }
}



// -----------------------------------------------------------------------------
//
// ldStripSysAttrs -
//
void ldStripSysAttrs(KjNode* treeP)
{
  if (treeP == NULL)
    return;

  if (treeP->type == KjObject)
  {
    stripObject(treeP);
  }
  else if (treeP->type == KjArray)
  {
    for (KjNode* itemP = treeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
      stripObject(itemP);
  }
}
