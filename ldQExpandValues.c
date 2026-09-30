//
// FILE            ldQExpandValues.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                     // bool
#include <stddef.h>                                      // NULL
#include <string.h>                                      // strcmp

#include "corAlloc/CorAlloc.h"                           // CorAlloc
#include "corJsonld/corLdExpand.h"                       // corLdExpand
#include "corNgsild/LdQ.h"                               // LdQNode, LdQBareWord, ...
#include "corNgsild/LdProblem.h"                         // LD_ERROR_BAD_REQUEST_DATA
#include "corNgsild/ldError.h"                           // ldError
#include "corNgsild/ldQExpandValues.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// named - does expandValues name this attribute?
//
static bool named(const char* attr, char** evV)
{
  if ((evV == NULL) || (attr == NULL))
    return false;

  for (int ix = 0; evV[ix] != NULL; ix++)
  {
    if (strcmp(attr, evV[ix]) == 0)
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// expandWalk - expand the string and bare-word values of every term expandValues names
//
static void expandWalk(LdQNode* nodeP, char** evV, CorLdContext* contextP, CorAlloc* kaP)
{
  if (nodeP == NULL)
    return;

  if (nodeP->type == LdQTermNode)
  {
    if ((named(nodeP->term.attr, evV) == false) || (nodeP->term.op == LdQPattern) || (nodeP->term.op == LdQNotPattern))
      return;

    if ((nodeP->term.valueType == LdQString) || (nodeP->term.valueType == LdQBareWord))
    {
      char* expanded = corLdExpand(contextP, nodeP->term.value.s, kaP, NULL, NULL);

      if (expanded != NULL)
        nodeP->term.value.s = expanded;

      nodeP->term.valueType = LdQString;             // a bare word expanded IS a string from here on
    }
    else if (nodeP->term.valueType == LdQValueList)
    {
      //
      // Every STRING (or bare-word) item, not the list as a whole: § 7.2.3.4 puts no requirement on
      // a list sharing a type (`a==1,"two"` is legal), and expanding a number or a bool would be
      // meaningless. Handling only LdQString once meant `q=category=="barn"` matched a VocabProperty
      // while `q=category=="barn","farm_auxiliary"` returned nothing.
      //
      for (int i = 0; i < nodeP->term.value.list.count; i++)
      {
        LdQValueType itemType = (nodeP->term.value.list.itemTypeV != NULL) ? nodeP->term.value.list.itemTypeV[i] : LdQString;

        if ((itemType != LdQString) && (itemType != LdQBareWord))
          continue;

        char* expanded = corLdExpand(contextP, nodeP->term.value.list.values[i], kaP, NULL, NULL);

        if (expanded != NULL)
          nodeP->term.value.list.values[i] = expanded;

        if (nodeP->term.value.list.itemTypeV != NULL)
          nodeP->term.value.list.itemTypeV[i] = LdQString;
      }
    }
  }
  else if ((nodeP->type == LdQAndNode) || (nodeP->type == LdQOrNode))
  {
    for (int i = 0; i < nodeP->group.count; i++)
      expandWalk(nodeP->group.childV[i], evV, contextP, kaP);
  }
  else if (nodeP->type == LdQLinkedNode)
    expandWalk(nodeP->linked.subQ, evV, contextP, kaP);
}



// -----------------------------------------------------------------------------
//
// bareWordLeft - the first bare word expandValues did not claim, or NULL
//
static const char* bareWordLeft(LdQNode* nodeP)
{
  if (nodeP == NULL)
    return NULL;

  if (nodeP->type == LdQTermNode)
  {
    if (nodeP->term.valueType == LdQBareWord)
      return nodeP->term.value.s;

    if ((nodeP->term.valueType == LdQValueList) && (nodeP->term.value.list.itemTypeV != NULL))
    {
      for (int i = 0; i < nodeP->term.value.list.count; i++)
      {
        if (nodeP->term.value.list.itemTypeV[i] == LdQBareWord)
          return nodeP->term.value.list.values[i];
      }
    }

    return NULL;
  }

  if ((nodeP->type == LdQAndNode) || (nodeP->type == LdQOrNode))
  {
    for (int i = 0; i < nodeP->group.count; i++)
    {
      const char* w = bareWordLeft(nodeP->group.childV[i]);

      if (w != NULL)
        return w;
    }
    return NULL;
  }

  if (nodeP->type == LdQLinkedNode)
    return bareWordLeft(nodeP->linked.subQ);

  return NULL;
}



// -----------------------------------------------------------------------------
//
// ldQLangProperties -
//
void ldQLangProperties(LdQNode* nodeP, char** lpV)
{
  if ((nodeP == NULL) || (lpV == NULL))
    return;

  if (nodeP->type == LdQTermNode)
  {
    if ((nodeP->term.valuePathN > 0) && (nodeP->term.valuePathRawV != NULL) && (named(nodeP->term.attr, lpV) == true))
    {
      for (int i = 0; i < nodeP->term.valuePathN; i++)
        nodeP->term.valuePathV[i] = nodeP->term.valuePathRawV[i];
    }
  }
  else if ((nodeP->type == LdQAndNode) || (nodeP->type == LdQOrNode))
  {
    for (int i = 0; i < nodeP->group.count; i++)
      ldQLangProperties(nodeP->group.childV[i], lpV);
  }
  else if (nodeP->type == LdQLinkedNode)
    ldQLangProperties(nodeP->linked.subQ, lpV);
}



// -----------------------------------------------------------------------------
//
// ldQExpandValues -
//
bool ldQExpandValues(LdQNode* qExpr, char** evV, CorLdContext* contextP, CorAlloc* kaP)
{
  if (qExpr == NULL)
    return true;

  if (evV != NULL)
    expandWalk(qExpr, evV, contextP, kaP);

  const char* word = bareWordLeft(qExpr);

  if (word != NULL)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid q parameter",
            "'%s' is not a valid value: a string must be quoted, and a number must not carry "
            "anything after it - unless expandValues names its attribute", word);
    return false;
  }

  return true;
}
