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
#include "corNgsild/LdQ.h"                               // LdQNode
#include "corNgsild/LdProblem.h"                         // LD_ERROR_BAD_REQUEST_DATA
#include "corNgsild/ldError.h"                           // ldError
#include "corNgsild/ldQueryParams.h"                     // ldParamSplit
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
// expandWalk - expand the string values of every term expandValues names
//
static void expandWalk(LdQNode* nodeP, char** evV, CorLdContext* contextP, CorAlloc* kaP)
{
  if (nodeP == NULL)
    return;

  if (nodeP->type == LdQTermNode)
  {
    if ((named(nodeP->term.attr, evV) == false) || (nodeP->term.op == LdQPattern) || (nodeP->term.op == LdQNotPattern))
      return;

    if (nodeP->term.valueType == LdQString)
    {
      char* expanded = corLdExpand(contextP, nodeP->term.value.s, kaP, NULL, NULL);

      if (expanded != NULL)
        nodeP->term.value.s = expanded;

    }
    else if (nodeP->term.valueType == LdQValueList)
    {
      //
      // Every STRING item, not the list as a whole: § 7.2.3.4 puts no requirement on
      // a list sharing a type (`a==1,"two"` is legal), and expanding a number or a bool would be
      // meaningless. Handling only LdQString once meant `q=category=="barn"` matched a VocabProperty
      // while `q=category=="barn","farm_auxiliary"` returned nothing.
      //
      for (int i = 0; i < nodeP->term.value.list.count; i++)
      {
        LdQValueType itemType = (nodeP->term.value.list.itemTypeV != NULL) ? nodeP->term.value.list.itemTypeV[i] : LdQString;

        if (itemType != LdQString)
          continue;

        char* expanded = corLdExpand(contextP, nodeP->term.value.list.values[i], kaP, NULL, NULL);

        if (expanded != NULL)
          nodeP->term.value.list.values[i] = expanded;

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
// ldAttrListExpand -
//
char** ldAttrListExpand(const char* csv, CorLdContext* contextP, CorAlloc* kaP)
{
  if ((csv == NULL) || (csv[0] == 0))
    return NULL;

  char** nameV = ldParamSplit(corAllocStrdup(kaP, csv), kaP);   // splits in place - a copy

  for (int ix = 0; (nameV != NULL) && (nameV[ix] != NULL); ix++)
  {
    char* expanded = corLdExpand(contextP, nameV[ix], kaP, NULL, NULL);

    if (expanded != NULL)
      nameV[ix] = expanded;
  }

  return nameV;
}



// -----------------------------------------------------------------------------
//
// ldQRawValuePaths -
//
void ldQRawValuePaths(LdQNode* nodeP, char** attrV)
{
  if ((nodeP == NULL) || (attrV == NULL))
    return;

  if (nodeP->type == LdQTermNode)
  {
    if ((nodeP->term.valuePathN > 0) && (nodeP->term.valuePathRawV != NULL) && (named(nodeP->term.attr, attrV) == true))
    {
      for (int i = 0; i < nodeP->term.valuePathN; i++)
        nodeP->term.valuePathV[i] = nodeP->term.valuePathRawV[i];
    }
  }
  else if ((nodeP->type == LdQAndNode) || (nodeP->type == LdQOrNode))
  {
    for (int i = 0; i < nodeP->group.count; i++)
      ldQRawValuePaths(nodeP->group.childV[i], attrV);
  }
  else if (nodeP->type == LdQLinkedNode)
    ldQRawValuePaths(nodeP->linked.subQ, attrV);
}



// -----------------------------------------------------------------------------
//
// ldQExpandValues -
//
void ldQExpandValues(LdQNode* qExpr, char** evV, CorLdContext* contextP, CorAlloc* kaP)
{
  if ((qExpr != NULL) && (evV != NULL))
    expandWalk(qExpr, evV, contextP, kaP);
}
