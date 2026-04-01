//
// FILE            ldCheckRegistration.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strcmp

#include "kbase/kLibLog.h"                             // KLOG_T
#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode

#include "swNgsild/LdOp.h"                               // LdOp
#include "swNgsild/LdCheck.h"                            // OBJECT_CHECK, ARRAY_CHECK, ...
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/ldTypes.h"                            // ldOpToString
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldCheckRegistration.h"                // Own interface
#include "swNgsild/ldTraceLevels.h"                      // LdTCheckReg



// -----------------------------------------------------------------------------
//
// checkInformationArray - validate the "information" array
//
static bool checkInformationArray(KjNode* infoArrayP)
{
  ARRAY_CHECK(infoArrayP, "Invalid Registration", "'information' must be a JSON array");
  EMPTY_ARRAY_CHECK(infoArrayP, "'information' must have at least one element");

  for (KjNode* infoP = infoArrayP->value.firstChildP; infoP != NULL; infoP = infoP->next)
  {
    OBJECT_CHECK(infoP, "Invalid Registration", "'information' items must be JSON objects");

    // Must have "entities"
    bool hasEntities = false;
    for (KjNode* fieldP = infoP->value.firstChildP; fieldP != NULL; fieldP = fieldP->next)
    {
      if (strcmp(fieldP->name, LD_VOCAB_ENTITIES) == 0)
      {
        hasEntities = true;
        ARRAY_CHECK(fieldP, "Invalid Registration", "'information[].entities' must be an array");
        EMPTY_ARRAY_CHECK(fieldP, "'information[].entities' must have at least one element");

        // Each entity must have at least "type"
        for (KjNode* entP = fieldP->value.firstChildP; entP != NULL; entP = entP->next)
        {
          OBJECT_CHECK(entP, "Invalid Registration", "'information[].entities[]' items must be JSON objects");

          bool hasType = false;
          for (KjNode* eFieldP = entP->value.firstChildP; eFieldP != NULL; eFieldP = eFieldP->next)
          {
            if (strcmp(eFieldP->name, "type") == 0)
              hasType = true;
          }
          if (hasType == false)
          {
            ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration", "Registration entity info must have 'type'");
            return false;
          }
        }
      }
    }

    if (hasEntities == false)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Registration", "'information' element must have 'entities'");
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// ldCheckRegistration -
//
bool ldCheckRegistration(KjNode* regP, LdOp op, KAlloc* faP)
{
  OBJECT_CHECK(regP, "Invalid Registration", "Registration payload must be a JSON object");

  KLOG_T(LdTCheckReg, "Checking registration payload for op %s", ldOpToString(op));

  KjNode*  typeP      = NULL;
  KjNode*  infoP      = NULL;
  KjNode*  endpointP  = NULL;
  KjNode*  modeP      = NULL;
  KjNode*  expiresAtP = NULL;

  for (KjNode* childP = regP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if      (strcmp(childP->name, "type")               == 0)  typeP      = childP;
    else if (strcmp(childP->name, LD_VOCAB_INFORMATION) == 0)  infoP      = childP;
    else if (strcmp(childP->name, LD_VOCAB_ENDPOINT)    == 0)  endpointP  = childP;
    else if (strcmp(childP->name, LD_VOCAB_MODE)        == 0)  modeP      = childP;
    else if (strcmp(childP->name, LD_VOCAB_EXPIRES_AT)  == 0)  expiresAtP = childP;
  }

  // "type" must be "ContextSourceRegistration" for create
  if (op == LdOpCreateRegistration)
  {
    MANDATORY_CHECK(typeP, "Missing Type", "Registration 'type' is mandatory for create");

    if (typeP->type != KjString || strcmp(typeP->value.s, "ContextSourceRegistration") != 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Type", "Registration 'type' must be 'ContextSourceRegistration'");
      return false;
    }
  }

  // "information" required for create
  if (op == LdOpCreateRegistration)
    MANDATORY_CHECK(infoP, "Missing Information", "Registration 'information' is mandatory for create");

  if (infoP != NULL)
  {
    if (checkInformationArray(infoP) == false)
      return false;
  }

  // "endpoint" required for create
  if (op == LdOpCreateRegistration)
    MANDATORY_CHECK(endpointP, "Missing Endpoint", "Registration 'endpoint' is mandatory for create");

  if (endpointP != NULL)
  {
    STRING_CHECK(endpointP, "Invalid Registration", "Registration 'endpoint' must be a URI string");
    URI_CHECK(endpointP->value.s);
  }

  // Validate "mode" if present
  if (modeP != NULL)
  {
    STRING_CHECK(modeP, "Invalid Registration", "Registration 'mode' must be a string");

    const char* mode = modeP->value.s;
    if (strcmp(mode, "inclusive") != 0 && strcmp(mode, "exclusive") != 0 && strcmp(mode, "redirect") != 0 && strcmp(mode, "auxiliary") != 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Mode", "Registration 'mode' must be one of: inclusive, exclusive, redirect, auxiliary (got '%s')", mode);
      return false;
    }
  }

  // Validate "expiresAt" if present
  if (expiresAtP != NULL)
  {
    STRING_CHECK(expiresAtP, "Invalid Registration", "'expiresAt' must be a DateTime string");
    DATETIME_CHECK(expiresAtP->value.s, "'expiresAt' is not a valid ISO 8601 DateTime");
  }

  KLOG_T(LdTCheckReg, "Registration payload valid");
  return true;
}
