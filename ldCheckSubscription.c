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
// checkEndpoint - validate the "notification.endpoint" object
//
static bool checkEndpoint(KjNode* endpointP)
{
  OBJECT_CHECK(endpointP, "Invalid Subscription", "'notification.endpoint' must be a JSON object");
  EMPTY_OBJECT_CHECK(endpointP, "'notification.endpoint' must not be empty");

  KjNode* uriP    = NULL;
  KjNode* acceptP = NULL;

  for (KjNode* childP = endpointP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, LD_VOCAB_URI) == 0)
    {
      DUPLICATE_CHECK(uriP, "notification.endpoint.uri", childP);
      STRING_CHECK(childP, "Invalid Subscription", "'notification.endpoint.uri' must be a string");
      if (childP->value.s[0] == 0)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription", "'notification.endpoint.uri' must not be empty");
        return false;
      }
      URI_CHECK(childP->value.s);
    }
    else if (strcmp(childP->name, "https://uri.etsi.org/ngsi-ld/accept") == 0 || strcmp(childP->name, "accept") == 0)
    {
      DUPLICATE_CHECK(acceptP, "notification.endpoint.accept", childP);
      STRING_CHECK(childP, "Invalid Subscription", "'notification.endpoint.accept' must be a string");
    }
    // receiverInfo and notifierInfo — accept but don't validate deeply for now
  }

  MANDATORY_CHECK(uriP, "Invalid Subscription", "'notification.endpoint.uri' is mandatory");

  return true;
}



// -----------------------------------------------------------------------------
//
// checkNotification - validate the "notification" object
//
static bool checkNotification(KjNode* notifP)
{
  OBJECT_CHECK(notifP, "Invalid Subscription", "'notification' must be a JSON object");
  EMPTY_OBJECT_CHECK(notifP, "'notification' must not be empty");

  KjNode* endpointP   = NULL;
  KjNode* formatP     = NULL;
  KjNode* attributesP = NULL;

  for (KjNode* childP = notifP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, LD_VOCAB_ENDPOINT) == 0)
    {
      DUPLICATE_CHECK(endpointP, "notification.endpoint", childP);
    }
    else if (strcmp(childP->name, LD_VOCAB_FORMAT) == 0 || strcmp(childP->name, "format") == 0)
    {
      DUPLICATE_CHECK(formatP, "notification.format", childP);
      STRING_CHECK(childP, "Invalid Subscription", "'notification.format' must be a string");
    }
    else if (strcmp(childP->name, LD_VOCAB_ATTRIBUTES) == 0 || strcmp(childP->name, "attributes") == 0)
    {
      DUPLICATE_CHECK(attributesP, "notification.attributes", childP);
      ARRAY_CHECK(childP, "Invalid Subscription", "'notification.attributes' must be an array");
      EMPTY_ARRAY_CHECK(childP, "'notification.attributes' must not be empty");

      // Each item must be a string
      for (KjNode* attrP = childP->value.firstChildP; attrP != NULL; attrP = attrP->next)
      {
        if (attrP->type != KjString)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription", "'notification.attributes' items must be strings");
          return false;
        }
      }
    }
    else if (strcmp(childP->name, "status") == 0 ||
             strcmp(childP->name, "timesSent") == 0 ||
             strcmp(childP->name, "timesFailed") == 0 ||
             strcmp(childP->name, "lastNotification") == 0 ||
             strcmp(childP->name, "lastSuccess") == 0 ||
             strcmp(childP->name, "lastFailure") == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Read-Only Field",
              "'notification.%s' is read-only and cannot be set", childP->name);
      return false;
    }
  }

  MANDATORY_CHECK(endpointP, "Invalid Subscription", "'notification.endpoint' is mandatory");

  if (checkEndpoint(endpointP) == false)
    return false;

  return true;
}



// -----------------------------------------------------------------------------
//
// checkEntitiesArray - validate the "entities" array
//
static bool checkEntitiesArray(KjNode* entitiesP)
{
  ARRAY_CHECK(entitiesP, "Invalid Subscription", "'entities' must be a JSON array");
  EMPTY_ARRAY_CHECK(entitiesP, "'entities' must not be empty");

  for (KjNode* entP = entitiesP->value.firstChildP; entP != NULL; entP = entP->next)
  {
    OBJECT_CHECK(entP, "Invalid Subscription", "'entities' items must be JSON objects");

    bool  hasType      = false;
    bool  hasId        = false;
    bool  hasIdPattern = false;

    for (KjNode* fieldP = entP->value.firstChildP; fieldP != NULL; fieldP = fieldP->next)
    {
      if (strcmp(fieldP->name, "type") == 0)
      {
        STRING_CHECK(fieldP, "Invalid Subscription", "'entities[].type' must be a string");
        if (fieldP->value.s[0] == 0)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription", "'entities[].type' must not be empty");
          return false;
        }
        hasType = true;
      }
      else if (strcmp(fieldP->name, "id") == 0)
      {
        STRING_CHECK(fieldP, "Invalid Subscription", "'entities[].id' must be a string");
        if (fieldP->value.s[0] == 0)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription", "'entities[].id' must not be empty");
          return false;
        }
        URI_CHECK(fieldP->value.s);
        hasId = true;
      }
      else if (strcmp(fieldP->name, LD_VOCAB_ID_PATTERN) == 0)
      {
        STRING_CHECK(fieldP, "Invalid Subscription", "'entities[].idPattern' must be a string");
        if (fieldP->value.s[0] == 0)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription", "'entities[].idPattern' must not be empty");
          return false;
        }
        hasIdPattern = true;
      }
    }

    if (hasType == false && hasId == false && hasIdPattern == false)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
              "'entities' item must have at least 'type', 'id', or 'idPattern'");
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// checkGeoQ - validate the "geoQ" object
//
static bool checkGeoQ(KjNode* geoQP)
{
  OBJECT_CHECK(geoQP, "Invalid Subscription", "'geoQ' must be a JSON object");

  KjNode* geometryP    = NULL;
  KjNode* coordinatesP = NULL;
  KjNode* georelP      = NULL;
  KjNode* geopropertyP = NULL;

  for (KjNode* childP = geoQP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    const char* name = childP->name;

    if (strcmp(name, "https://purl.org/geojson/vocab#geometry") == 0 || strcmp(name, "geometry") == 0)
    {
      DUPLICATE_CHECK(geometryP, "geoQ.geometry", childP);
      STRING_CHECK(childP, "Invalid Subscription", "'geoQ.geometry' must be a string");

      // Valid geometry types
      const char* v = childP->value.s;
      if (strcmp(v, "Point") != 0 && strcmp(v, "MultiPoint") != 0 &&
          strcmp(v, "LineString") != 0 && strcmp(v, "MultiLineString") != 0 &&
          strcmp(v, "Polygon") != 0 && strcmp(v, "MultiPolygon") != 0)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
                "'geoQ.geometry' has invalid value '%s'", v);
        return false;
      }
    }
    else if (strcmp(name, "https://purl.org/geojson/vocab#coordinates") == 0 || strcmp(name, "coordinates") == 0)
    {
      DUPLICATE_CHECK(coordinatesP, "geoQ.coordinates", childP);
      // coordinates can be a string (JSON-encoded) or an array
      if (childP->type != KjString && childP->type != KjArray)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
                "'geoQ.coordinates' must be an array or string");
        return false;
      }
    }
    else if (strcmp(name, "https://uri.etsi.org/ngsi-ld/georel") == 0 || strcmp(name, "georel") == 0)
    {
      DUPLICATE_CHECK(georelP, "geoQ.georel", childP);
      STRING_CHECK(childP, "Invalid Subscription", "'geoQ.georel' must be a string");
    }
    else if (strcmp(name, "https://uri.etsi.org/ngsi-ld/geoproperty") == 0 || strcmp(name, "geoproperty") == 0)
    {
      DUPLICATE_CHECK(geopropertyP, "geoQ.geoproperty", childP);
      STRING_CHECK(childP, "Invalid Subscription", "'geoQ.geoproperty' must be a string");
    }
  }

  // All three are required together — if geoQ is present, all must be present
  if (geometryP == NULL || coordinatesP == NULL || georelP == NULL)
  {
    const char* missing = (geometryP == NULL) ? "geometry" : (coordinatesP == NULL) ? "coordinates" : "georel";
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
            "'geoQ' requires 'geometry', 'coordinates', and 'georel' — missing '%s'", missing);
    return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// ldCheckSubscription -
//
bool ldCheckSubscription(KjNode* subP, LdOp op, KAlloc* kaP)
{
  OBJECT_CHECK(subP, "Invalid Subscription", "Subscription payload must be a JSON object");

  KLOG_T(LdTCheckSub, "Checking subscription payload for op %s", ldOpToString(op));

  KjNode*  typeP          = NULL;
  KjNode*  idP            = NULL;
  KjNode*  entitiesP      = NULL;
  KjNode*  watchedAttrsP  = NULL;
  KjNode*  timeIntervalP  = NULL;
  KjNode*  notificationP  = NULL;
  KjNode*  throttlingP    = NULL;
  KjNode*  expiresAtP     = NULL;
  KjNode*  isActiveP      = NULL;
  KjNode*  qP             = NULL;
  KjNode*  geoQP          = NULL;
  KjNode*  scopeQP        = NULL;
  KjNode*  nameP          = NULL;
  KjNode*  descriptionP   = NULL;

  for (KjNode* childP = subP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    const char* name = childP->name;

    if (strcmp(name, "type") == 0)
      DUPLICATE_CHECK(typeP, "type", childP);
    else if (strcmp(name, "id") == 0)
      DUPLICATE_CHECK(idP, "id", childP);
    else if (strcmp(name, LD_VOCAB_ENTITIES) == 0)
      DUPLICATE_CHECK(entitiesP, "entities", childP);
    else if (strcmp(name, LD_VOCAB_WATCHED_ATTRS) == 0)
      DUPLICATE_CHECK(watchedAttrsP, "watchedAttributes", childP);
    else if (strcmp(name, "https://uri.etsi.org/ngsi-ld/timeInterval") == 0 || strcmp(name, "timeInterval") == 0)
      DUPLICATE_CHECK(timeIntervalP, "timeInterval", childP);
    else if (strcmp(name, LD_VOCAB_NOTIFICATION) == 0)
      DUPLICATE_CHECK(notificationP, "notification", childP);
    else if (strcmp(name, LD_VOCAB_THROTTLING) == 0)
      DUPLICATE_CHECK(throttlingP, "throttling", childP);
    else if (strcmp(name, LD_VOCAB_EXPIRES_AT) == 0)
      DUPLICATE_CHECK(expiresAtP, "expiresAt", childP);
    else if (strcmp(name, LD_VOCAB_IS_ACTIVE) == 0)
      DUPLICATE_CHECK(isActiveP, "isActive", childP);
    else if (strcmp(name, "https://uri.etsi.org/ngsi-ld/q") == 0 || strcmp(name, "q") == 0)
      DUPLICATE_CHECK(qP, "q", childP);
    else if (strcmp(name, "https://uri.etsi.org/ngsi-ld/geoQ") == 0 || strcmp(name, "geoQ") == 0)
      DUPLICATE_CHECK(geoQP, "geoQ", childP);
    else if (strcmp(name, "https://uri.etsi.org/ngsi-ld/scopeQ") == 0 || strcmp(name, "scopeQ") == 0)
      DUPLICATE_CHECK(scopeQP, "scopeQ", childP);
    else if (strcmp(name, "https://uri.etsi.org/ngsi-ld/subscriptionName") == 0 || strcmp(name, "subscriptionName") == 0)
      DUPLICATE_CHECK(nameP, "subscriptionName", childP);
    else if (strcmp(name, "https://uri.etsi.org/ngsi-ld/description") == 0 || strcmp(name, "description") == 0)
      DUPLICATE_CHECK(descriptionP, "description", childP);
    else if (strcmp(name, LD_VOCAB_STATUS) == 0 || strcmp(name, "status") == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Read-Only Field", "'status' is read-only and cannot be set");
      return false;
    }
    else if (strcmp(name, LD_VOCAB_DATASET_ID) == 0)
    {
      // datasetId: array of URIs + "@none" — restricts which attribute instances
      // are included in notifications (§ 5.8.6).
      if (childP->type != KjArray)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription", "'datasetId' must be a JSON array");
        return false;
      }
      for (KjNode* dsP = childP->value.firstChildP; dsP != NULL; dsP = dsP->next)
      {
        if (dsP->type != KjString)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription", "'datasetId' items must be strings");
          return false;
        }
      }
    }
    else if (strcmp(name, "jsonldContext") == 0)
    {
      // Internal field — ignore silently
    }
  }

  //
  // "type" validation
  //
  if (op == LdOpCreateSubscription)
  {
    MANDATORY_CHECK(typeP, "Missing Type", "Subscription 'type' is mandatory for create");

    if (typeP->type != KjString
        || (strcmp(typeP->value.s, "Subscription") != 0
            && strcmp(typeP->value.s, "https://uri.etsi.org/ngsi-ld/Subscription") != 0))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Type", "Subscription 'type' must be 'Subscription'");
      return false;
    }
  }
  else if (typeP != NULL)
  {
    // PATCH must not modify type
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Read-Only Field", "'type' cannot be modified");
    return false;
  }

  //
  // "id" — not allowed in PATCH
  //
  if (op == LdOpUpdateSubscription && idP != NULL)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Read-Only Field", "'id' cannot be modified");
    return false;
  }

  //
  // "entities" or "watchedAttributes" required for create
  //
  if (op == LdOpCreateSubscription)
  {
    if (entitiesP == NULL && watchedAttrsP == NULL)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Missing Filter",
              "Subscription must have 'entities' or 'watchedAttributes'");
      return false;
    }
  }

  //
  // entities validation
  //
  if (entitiesP != NULL)
  {
    if (checkEntitiesArray(entitiesP) == false)
      return false;
  }

  //
  // watchedAttributes validation
  //
  if (watchedAttrsP != NULL)
  {
    ARRAY_CHECK(watchedAttrsP, "Invalid Subscription", "'watchedAttributes' must be an array");
    EMPTY_ARRAY_CHECK(watchedAttrsP, "'watchedAttributes' must not be empty");

    for (KjNode* wP = watchedAttrsP->value.firstChildP; wP != NULL; wP = wP->next)
    {
      if (wP->type != KjString)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
                "'watchedAttributes' items must be strings");
        return false;
      }
    }
  }

  //
  // timeInterval conflicts: watchedAttributes and throttling are incompatible
  // with periodic subscriptions (§ 5.2.12)
  //
  if (timeIntervalP != NULL && watchedAttrsP != NULL)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Incompatible Fields",
            "'timeInterval' and 'watchedAttributes' are mutually exclusive");
    return false;
  }
  if (timeIntervalP != NULL && throttlingP != NULL)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Incompatible Fields",
            "'timeInterval' and 'throttling' are mutually exclusive");
    return false;
  }

  //
  // timeInterval validation
  //
  if (timeIntervalP != NULL)
  {
    NUMBER_CHECK(timeIntervalP, "Invalid Subscription", "'timeInterval' must be a number");
    POSITIVE_NUMBER_CHECK(timeIntervalP, "'timeInterval' must be >= 0");
  }

  //
  // "notification" mandatory for create
  //
  if (op == LdOpCreateSubscription)
    MANDATORY_CHECK(notificationP, "Missing Notification", "Subscription 'notification' is mandatory for create");

  if (notificationP != NULL)
  {
    if (checkNotification(notificationP) == false)
      return false;
  }

  //
  // "throttling" validation
  //
  if (throttlingP != NULL)
  {
    NUMBER_CHECK(throttlingP, "Invalid Subscription", "'throttling' must be a number");
    POSITIVE_NUMBER_CHECK(throttlingP, "'throttling' must be >= 0");
  }

  //
  // "expiresAt" validation
  //
  if (expiresAtP != NULL)
  {
    STRING_CHECK(expiresAtP, "Invalid Subscription", "'expiresAt' must be a DateTime string");
    DATETIME_CHECK(expiresAtP->value.s, "'expiresAt' is not a valid ISO 8601 DateTime");
  }

  //
  // "isActive" must be boolean
  //
  if (isActiveP != NULL)
  {
    if (isActiveP->type != KjBoolean)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription", "'isActive' must be a boolean");
      return false;
    }
  }

  //
  // "q" must be string
  //
  if (qP != NULL)
  {
    STRING_CHECK(qP, "Invalid Subscription", "'q' must be a string");
  }

  //
  // "geoQ" validation
  //
  if (geoQP != NULL)
  {
    if (checkGeoQ(geoQP) == false)
      return false;
  }

  //
  // "scopeQ" must be string
  //
  if (scopeQP != NULL)
  {
    STRING_CHECK(scopeQP, "Invalid Subscription", "'scopeQ' must be a string");
  }

  //
  // "subscriptionName" must be string
  //
  if (nameP != NULL)
  {
    STRING_CHECK(nameP, "Invalid Subscription", "'subscriptionName' must be a string");
  }

  //
  // "description" must be string
  //
  if (descriptionP != NULL)
  {
    STRING_CHECK(descriptionP, "Invalid Subscription", "'description' must be a string");
  }

  KLOG_T(LdTCheckSub, "Subscription payload valid");
  return true;
}
