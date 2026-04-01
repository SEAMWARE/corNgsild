//
// FILE            ldCheckSubscription.c
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
#include "swNgsild/LdCheck.h"                            // OBJECT_CHECK, STRING_CHECK, ...
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/ldTypes.h"                            // ldOpToString
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldCheckSubscription.h"                // Own interface
#include "swNgsild/ldTraceLevels.h"                      // LdTCheckSub



// -----------------------------------------------------------------------------
//
// checkNotification - validate the "notification" object
//
static bool checkNotification(KjNode* notifP)
{
  OBJECT_CHECK(notifP, "Invalid Subscription", "'notification' must be a JSON object");

  KjNode* endpointP = NULL;

  for (KjNode* childP = notifP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, LD_VOCAB_ENDPOINT) == 0)
      endpointP = childP;
    else if (strcmp(childP->name, LD_VOCAB_FORMAT) == 0)
      STRING_CHECK(childP, "Invalid Subscription", "'notification.format' must be a string");
    else if (strcmp(childP->name, LD_VOCAB_ATTRIBUTES) == 0)
      ARRAY_CHECK(childP, "Invalid Subscription", "'notification.attributes' must be an array");
  }

  MANDATORY_CHECK(endpointP, "Invalid Subscription", "'notification.endpoint' is mandatory");
  OBJECT_CHECK(endpointP, "Invalid Subscription", "'notification.endpoint' must be a JSON object");

  // Check endpoint.uri
  KjNode* uriP = NULL;
  for (KjNode* childP = endpointP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, LD_VOCAB_URI) == 0)
      uriP = childP;
  }

  MANDATORY_CHECK(uriP, "Invalid Subscription", "'notification.endpoint.uri' is mandatory");
  STRING_CHECK(uriP, "Invalid Subscription", "'notification.endpoint.uri' must be a string");
  URI_CHECK(uriP->value.s);

  return true;
}



// -----------------------------------------------------------------------------
//
// checkEntitiesArray - validate the "entities" array
//
static bool checkEntitiesArray(KjNode* entitiesP)
{
  ARRAY_CHECK(entitiesP, "Invalid Subscription", "'entities' must be a JSON array");

  for (KjNode* entP = entitiesP->value.firstChildP; entP != NULL; entP = entP->next)
  {
    OBJECT_CHECK(entP, "Invalid Subscription", "'entities' items must be JSON objects");

    // Each entity info must have at least "type" or "id" or "idPattern"
    bool  hasType      = false;
    bool  hasId        = false;
    bool  hasIdPattern = false;

    for (KjNode* fieldP = entP->value.firstChildP; fieldP != NULL; fieldP = fieldP->next)
    {
      if (strcmp(fieldP->name, "type") == 0)                   hasType      = true;
      else if (strcmp(fieldP->name, "id") == 0)                hasId        = true;
      else if (strcmp(fieldP->name, LD_VOCAB_ID_PATTERN) == 0) hasIdPattern = true;
    }

    if (hasType == false && hasId == false && hasIdPattern == false)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription", "'entities' item must have at least 'type', 'id', or 'idPattern'");
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// ldCheckSubscription -
//
bool ldCheckSubscription(KjNode* subP, LdOp op, KAlloc* faP)
{
  OBJECT_CHECK(subP, "Invalid Subscription", "Subscription payload must be a JSON object");

  KLOG_T(LdTCheckSub, "Checking subscription payload for op %s", ldOpToString(op));

  KjNode*  typeP          = NULL;
  KjNode*  entitiesP      = NULL;
  KjNode*  watchedAttrsP  = NULL;
  KjNode*  notificationP  = NULL;
  KjNode*  throttlingP    = NULL;
  KjNode*  expiresAtP     = NULL;

  for (KjNode* childP = subP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if      (strcmp(childP->name, "type")                == 0)  typeP         = childP;
    else if (strcmp(childP->name, LD_VOCAB_ENTITIES)     == 0)  entitiesP     = childP;
    else if (strcmp(childP->name, LD_VOCAB_WATCHED_ATTRS)== 0)  watchedAttrsP = childP;
    else if (strcmp(childP->name, LD_VOCAB_NOTIFICATION) == 0)  notificationP = childP;
    else if (strcmp(childP->name, LD_VOCAB_THROTTLING)   == 0)  throttlingP   = childP;
    else if (strcmp(childP->name, LD_VOCAB_EXPIRES_AT)   == 0)  expiresAtP    = childP;
  }

  // "type" must be "Subscription" for create
  if (op == LdOpCreateSubscription)
  {
    MANDATORY_CHECK(typeP, "Missing Type", "Subscription 'type' is mandatory for create");

    if (typeP->type != KjString || strcmp(typeP->value.s, "Subscription") != 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Type", "Subscription 'type' must be 'Subscription'");
      return false;
    }
  }

  // "entities" or "watchedAttributes" required for create
  if (op == LdOpCreateSubscription)
  {
    if (entitiesP == NULL && watchedAttrsP == NULL)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Missing Filter", "Subscription must have 'entities' or 'watchedAttributes'");
      return false;
    }
  }

  if (entitiesP != NULL)
  {
    if (checkEntitiesArray(entitiesP) == false)
      return false;
  }

  if (watchedAttrsP != NULL)
    ARRAY_CHECK(watchedAttrsP, "Invalid Subscription", "'watchedAttributes' must be an array");

  // "notification" mandatory for create
  if (op == LdOpCreateSubscription)
    MANDATORY_CHECK(notificationP, "Missing Notification", "Subscription 'notification' is mandatory for create");

  if (notificationP != NULL)
  {
    if (checkNotification(notificationP) == false)
      return false;
  }

  // Validate "throttling"
  if (throttlingP != NULL)
  {
    NUMBER_CHECK(throttlingP, "Invalid Subscription", "'throttling' must be a number");
    POSITIVE_NUMBER_CHECK(throttlingP, "'throttling' must be >= 0");
  }

  // Validate "expiresAt"
  if (expiresAtP != NULL)
  {
    STRING_CHECK(expiresAtP, "Invalid Subscription", "'expiresAt' must be a DateTime string");
    DATETIME_CHECK(expiresAtP->value.s, "'expiresAt' is not a valid ISO 8601 DateTime");
  }

  KLOG_T(LdTCheckSub, "Subscription payload valid");
  return true;
}
