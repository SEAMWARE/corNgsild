//
// FILE            ldProj.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// § 4.21 NGSI-LD Attribute Projection Language — recursive-descent parser.
//
//   ProjectionTerm   = AttrName *1(LinkedEntityTerm) *(orOp ProjectionTerm)
//   LinkedEntityTerm = "{" ProjectionTerm "}"
//   orOp             = "," | "|"
//
#include <stdbool.h>                                 // bool, true, false
#include <stddef.h>                                  // NULL
#include <string.h>                                  // strcmp, memcpy

#include "kalloc/kaAlloc.h"                          // kaAlloc, kaStrdup
#include "swNgsild/LdProj.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// itemNew - allocate a fresh LdProjItem
//
static LdProjItem* itemNew(KAlloc* kaP, const char* nameStart, int nameLen)
{
  LdProjItem* itemP = (LdProjItem*) kaAlloc(kaP, sizeof(LdProjItem));
  if (itemP == NULL)
    return NULL;

  char* name = (char*) kaAlloc(kaP, nameLen + 1);
  if (name == NULL)
    return NULL;
  memcpy(name, nameStart, nameLen);
  name[nameLen] = '\0';

  itemP->name  = name;
  itemP->child = NULL;
  itemP->next  = NULL;
  return itemP;
}



// -----------------------------------------------------------------------------
//
// parseList - parse a comma/pipe-separated list of ProjectionTerms.
//
// Advances *pP past the consumed input. Stops at end-of-string or at a
// closing '}'. Returns the head of the list (NULL on syntax error, with
// *errMsgP set).
//
static LdProjItem* parseList(char** pP, KAlloc* kaP, const char** errMsgP)
{
  LdProjItem* head = NULL;
  LdProjItem* tail = NULL;
  char*       p    = *pP;

  while (true)
  {
    // Skip leading separator-free whitespace (defensive; URL params shouldn't carry any)
    while (*p == ' ' || *p == '\t')
      p++;

    if (*p == 0 || *p == '}')
      break;

    // Read attribute name: anything up to ',' '|' '{' '}' or end.
    char* nameStart = p;
    while (*p != 0 && *p != ',' && *p != '|' && *p != '{' && *p != '}')
      p++;

    int nameLen = (int)(p - nameStart);
    if (nameLen == 0)
    {
      *errMsgP = "empty attribute name in projection";
      return NULL;
    }

    LdProjItem* itemP = itemNew(kaP, nameStart, nameLen);
    if (itemP == NULL)
    {
      *errMsgP = "out of memory parsing projection";
      return NULL;
    }

    // Optional sub-projection: '{' ProjectionTerm '}'
    if (*p == '{')
    {
      p++;  // consume '{'
      itemP->child = parseList(&p, kaP, errMsgP);
      if (itemP->child == NULL && *errMsgP != NULL)
        return NULL;
      if (itemP->child == NULL)
      {
        // `attr{}` — LinkedEntityTerm requires a non-empty ProjectionTerm.
        *errMsgP = "empty linked-entity projection";
        return NULL;
      }
      if (*p != '}')
      {
        *errMsgP = "missing '}' in linked-entity projection";
        return NULL;
      }
      p++;  // consume '}'
    }

    if (head == NULL) head = itemP;
    else              tail->next = itemP;
    tail = itemP;

    if (*p == ',' || *p == '|')
    {
      p++;
      continue;
    }
    break;
  }

  *pP = p;
  return head;
}



// -----------------------------------------------------------------------------
//
// ldProjectionParse -
//
LdProjItem* ldProjectionParse(char* value, KAlloc* kaP, const char** errMsgP)
{
  static const char* noErr = NULL;
  if (errMsgP == NULL)
    errMsgP = &noErr;
  *errMsgP = NULL;

  if (value == NULL || value[0] == 0)
    return NULL;

  char*       p    = value;
  LdProjItem* head = parseList(&p, kaP, errMsgP);

  if (head != NULL && *p != 0)
  {
    *errMsgP = "trailing characters in projection";
    return NULL;
  }

  return head;
}



// -----------------------------------------------------------------------------
//
// ldProjectionTopLevelNames -
//
char** ldProjectionTopLevelNames(LdProjItem* tree, KAlloc* kaP, bool includeNested)
{
  if (tree == NULL)
    return NULL;

  int count = 0;
  for (LdProjItem* itemP = tree; itemP != NULL; itemP = itemP->next)
    if (includeNested || itemP->child == NULL)
      count++;

  if (count == 0)
    return NULL;

  char** result = (char**) kaAlloc(kaP, (count + 1) * sizeof(char*));
  if (result == NULL)
    return NULL;

  int ix = 0;
  for (LdProjItem* itemP = tree; itemP != NULL; itemP = itemP->next)
    if (includeNested || itemP->child == NULL)
      result[ix++] = itemP->name;
  result[ix] = NULL;
  return result;
}



// -----------------------------------------------------------------------------
//
// ldProjectionFindChild -
//
LdProjItem* ldProjectionFindChild(LdProjItem* tree, const char* name)
{
  if (tree == NULL || name == NULL)
    return NULL;

  for (LdProjItem* itemP = tree; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->name != NULL && strcmp(itemP->name, name) == 0)
      return itemP->child;
  }
  return NULL;
}
