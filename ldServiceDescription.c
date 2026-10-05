//
// FILE            ldServiceDescription.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <string.h>                                   // strcmp

#include "corTree/CorNode.h"                          // CorNode
#include "corTree/corTreeLookup.h"                    // corTreeLookup

#include "corNgsild/LdProblem.h"                      // LD_ERROR_BAD_REQUEST_DATA
#include "corNgsild/ldError.h"                        // ldError
#include "corNgsild/ldCheckUri.h"                     // ldUriValid
#include "corNgsild/ldServiceDescription.h"           // Own interface



bool ldServiceDescriptionAccepted = false;



// -----------------------------------------------------------------------------
//
// ldServiceDescriptionIs -
//
bool ldServiceDescriptionIs(CorNode* attrP)
{
  if ((attrP == NULL) || (attrP->type != CorObject))
    return false;

  CorNode* typeP = corTreeLookup(attrP, "type");

  return (typeP != NULL) && (typeP->type == CorString) && (strcmp(typeP->value.s, "ServiceDescription") == 0);
}



// -----------------------------------------------------------------------------
//
// wrongType - a member of the wrong JSON type: 400, false
//
static bool wrongType(CorNode* attrP, CorNode* mP, const char* what)
{
  ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Attribute", "ServiceDescription '%s': '%s' must be %s", attrP->name, mP->name, what);
  return false;
}



// -----------------------------------------------------------------------------
//
// ldServiceDescriptionCheck -
//
bool ldServiceDescriptionCheck(CorNode* attrP)
{
  if (ldServiceDescriptionAccepted == false)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Attribute Type", "attribute '%s': 'ServiceDescription' is not an NGSI-LD Attribute type", attrP->name);
    return false;
  }

  bool endpoint = false;

  for (CorNode* mP = attrP->value.head; mP != NULL; mP = mP->next)
  {
    const char* name = mP->name;

    if ((strcmp(name, "type") == 0) || (strcmp(name, "createdAt") == 0) || (strcmp(name, "modifiedAt") == 0))
      continue;
    else if ((strcmp(name, "title") == 0) || (strcmp(name, "description") == 0))
    {
      if (mP->type != CorString)
        return wrongType(attrP, mP, "a string");
    }
    else if (strcmp(name, "mode") == 0)
    {
      if ((mP->type != CorString) || ((strcmp(mP->value.s, "synchronous") != 0) && (strcmp(mP->value.s, "asynchronous") != 0)))
        return wrongType(attrP, mP, "\"synchronous\" or \"asynchronous\"");
    }
    else if ((strcmp(name, "inputSchema") == 0) || (strcmp(name, "outputSchema") == 0))
    {
      if ((mP->type != CorObject) && (mP->type != CorBoolean))
        return wrongType(attrP, mP, "a JSON Schema");
    }
    else if (strcmp(name, "endpoint") == 0)
    {
      if ((mP->type != CorString) || (ldUriValid(mP->value.s) == false))
        return wrongType(attrP, mP, "a URI - where the service is executed");
      endpoint = true;
    }
    else if (strcmp(name, "serviceDescriptionInEntity") == 0)
    {
      if (mP->type != CorBoolean)
        return wrongType(attrP, mP, "true or false");
    }
    else
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Attribute", "ServiceDescription '%s': '%s' is not one of its members", attrP->name, name);
      return false;
    }
  }

  if (endpoint == false)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Attribute", "ServiceDescription '%s' needs its 'endpoint' - where the service is executed", attrP->name);
    return false;
  }

  return true;
}
