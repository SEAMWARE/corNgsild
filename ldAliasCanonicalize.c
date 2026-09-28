//
// FILE            ldAliasCanonicalize.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strcmp

#include "corTree/CorNode.h"                            // CorNode

#include "corJsonld/corLdInit.h"                        // corLdCoreContext
#include "corJsonld/corLdCompact.h"                     // corLdCompact

#include "corNgsild/CorNgsild.h"                        // corNgsild
#include "corNgsild/LdProblem.h"                        // LD_ERROR_BAD_REQUEST_DATA
#include "corNgsild/ldError.h"                          // ldError
#include "corNgsild/ldTermId.h"                         // ldTermId, ldNodeRename, CorTerm*
#include "corNgsild/ldTermClass.h"                      // ldTermClass, LD_TC_VALUE_KEY
#include "corNgsild/ldAliasCanonicalize.h"              // Own interface



static bool canonicalizeArray(CorNode* arrayP);



// -----------------------------------------------------------------------------
//
// isValueObject - a JSON-LD value object: has an "@value" member
//
static bool isValueObject(CorNode* objectP)
{
  for (CorNode* childP = objectP->value.head; childP != NULL; childP = childP->next)
  {
    if ((childP->name != NULL) && (childP->name[0] == '@') && (strcmp(childP->name, "@value") == 0))
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// canonicalizeObject -
//
static bool canonicalizeObject(CorNode* objectP, bool topLevel)
{
  //
  // corJson names the top-level container "toplevel object" - not a name to show a client
  //
  const char* where = (topLevel == true) ? "the payload" : ((objectP->name != NULL) && (objectP->name[0] != 0)) ? objectP->name : "an array item";

  //
  // The error names what the client SENT - an attribute's name is expanded by now
  //
  if ((topLevel == false) && (objectP->name != NULL) && (objectP->name[0] != 0))
  {
    const char* sentP = corLdCompact((corNgsild.contextP != NULL) ? corNgsild.contextP : corLdCoreContext(), objectP->name);

    if (sentP != NULL)
      where = sentP;
  }

  if (isValueObject(objectP) == true)
    return true;

  CorNode* idP   = NULL;
  CorNode* typeP = NULL;

  for (CorNode* childP = objectP->value.head; childP != NULL; childP = childP->next)
  {
    if (childP->name == NULL)
      continue;

    CorTerm term = ldTermId(childP);   // "@id"/"@type" resolve to id/type

    if (term == CorTermId)
    {
      if (idP != NULL)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Duplicate Id", "Duplicate 'id' / '@id' in %s%s%s", (topLevel == true) ? "" : "'", where, (topLevel == true) ? "" : "'");
        return false;
      }
      idP = childP;

      if (childP->name[0] == '@')
        ldNodeRename(childP, "id");
      continue;
    }

    if (term == CorTermType)
    {
      if (typeP != NULL)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Duplicate Type", "Duplicate 'type' / '@type' in %s%s%s", (topLevel == true) ? "" : "'", where, (topLevel == true) ? "" : "'");
        return false;
      }
      typeP = childP;

      if (childP->name[0] == '@')
        ldNodeRename(childP, "type");
      continue;
    }

    //
    // Down into anything that is not a value and not a JSON-LD keyword member
    //
    if ((childP->name[0] == '@') || ((ldTermClass[term] & LD_TC_VALUE_KEY) != 0))
      continue;

    if ((childP->type == CorObject) && (canonicalizeObject(childP, false) == false))
      return false;

    if ((childP->type == CorArray) && (canonicalizeArray(childP) == false))
      return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// canonicalizeArray - the objects of an array (a batch, entity selectors, multi-attribute instances)
//
static bool canonicalizeArray(CorNode* arrayP)
{
  for (CorNode* itemP = arrayP->value.head; itemP != NULL; itemP = itemP->next)
  {
    if ((itemP->type == CorObject) && (canonicalizeObject(itemP, false) == false))
      return false;

    if ((itemP->type == CorArray) && (canonicalizeArray(itemP) == false))
      return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// ldAliasCanonicalize -
//
bool ldAliasCanonicalize(CorNode* treeP)
{
  if (treeP == NULL)
    return true;

  if (treeP->type == CorObject)
    return canonicalizeObject(treeP, true);

  if (treeP->type == CorArray)
    return canonicalizeArray(treeP);

  return true;
}
