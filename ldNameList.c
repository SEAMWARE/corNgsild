//
// FILE            ldNameList.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                     // bool
#include <stddef.h>                                      // NULL
#include <string.h>                                      // strlen, strchr, memcpy

#include "corAlloc/CorAlloc.h"                           // CorAlloc
#include "corAlloc/corAlloc.h"                           // corAlloc
#include "corTree/CorNode.h"                             // CorNode, CorString, CorArray
#include "corNgsild/LdProblem.h"                         // LD_ERROR_BAD_REQUEST_DATA
#include "corNgsild/ldError.h"                           // ldError
#include "corNgsild/ldNameList.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// ldNameListCheck -
//
bool ldNameListCheck(CorNode* nodeP, const char* title, const char* member)
{
  if (nodeP->type == CorString)
    return true;

  if (nodeP->type != CorArray)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, title, "'%s' must be a string or an array of strings", member);
    return false;
  }

  if (nodeP->value.head == NULL)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, title, "'%s': an empty array", member);
    return false;
  }

  for (CorNode* itemP = nodeP->value.head; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->type != CorString)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, title, "'%s': each item of the array must be a string", member);
      return false;
    }

    if (itemP->value.s[0] == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, title, "'%s': an item of the array is empty", member);
      return false;
    }

    if (strchr(itemP->value.s, ',') != NULL)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, title, "'%s': each item of the array is one name, not a comma separated list: '%s'", member, itemP->value.s);
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// ldNameListCsv -
//
const char* ldNameListCsv(CorNode* nodeP, CorAlloc* kaP)
{
  if (nodeP == NULL)
    return NULL;

  if (nodeP->type == CorString)
    return nodeP->value.s;

  if (nodeP->type != CorArray)
    return NULL;

  int size = 0;
  for (CorNode* itemP = nodeP->value.head; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->type == CorString)
      size += strlen(itemP->value.s) + 1;
  }

  if (size == 0)
    return NULL;

  char* csv = (char*) corAlloc(kaP, size);
  int   pos = 0;

  for (CorNode* itemP = nodeP->value.head; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->type != CorString)
      continue;

    if (pos > 0)
      csv[pos++] = ',';

    int len = strlen(itemP->value.s);
    memcpy(&csv[pos], itemP->value.s, len);
    pos += len;
  }
  csv[pos] = 0;

  return csv;
}
