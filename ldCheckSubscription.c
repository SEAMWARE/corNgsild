//
// FILE            ldCheckSubscription.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
//
#include <stdbool.h>                                     // bool
#include <stdlib.h>                                      // free, calloc
#include <string.h>                                      // strcmp
#include <time.h>                                        // time

#include "kbase/kLibLog.h"                             // KLOG_T
#include "kalloc/KAlloc.h"                             // KAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kalloc/kaStrdup.h"                           // kaStrdup
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjLookup.h"                            // kjLookup
#include "kjson/kjBuilder.h"                           // kjChildRemove
#include "kjson/kjParse.h"                             // kjParse
#include "kjson/kjBufferCreate.h"                      // kjBufferCreate
#include "kjson/kjRender.h"                            // kjFastRender
#include "kjson/kjRenderSize.h"                        // kjFastRenderSize

#include "swNgsild/LdOp.h"                               // LdOp
#include "swNgsild/SwNgsild.h"                           // swNgsild
#include "swNgsild/LdTypeExpr.h"                         // ldTypeExprParse, ldTypeExprFree
#include "swNgsild/LdCheck.h"                            // OBJECT_CHECK, STRING_CHECK, ...
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/ldQParse.h"                           // ldQParse
#include "swNgsild/ldCheckGeo.h"                          // ldCheckGeoQuery
#include "swNgsild/ldTypes.h"                            // ldOpToString
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldCheckSubscription.h"                // Own interface
#include "swNgsild/ldConformanceDowngrade.h"             // ldConformanceParse
#include "swNgsild/ldTraceLevels.h"                      // LdTCheckSub



// -----------------------------------------------------------------------------
//
// checkNotifierInfo - validate notification.endpoint.notifierInfo (§ 5.2.15 / § 7.2 Table 7.2-1)
//
// Each item must be {key:string, value:string}. MQTT-QoS must be 0/1/2;
// MQTT-Version must be mqtt3.1.1 or mqtt5.0.
//
static bool checkNotifierInfo(KjNode* niP)
{
  ARRAY_CHECK(niP, "Invalid Subscription", "'notification.endpoint.notifierInfo' must be an array");

  for (KjNode* kvP = niP->value.firstChildP; kvP != NULL; kvP = kvP->next)
  {
    if (kvP->type != KjObject)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
              "'notification.endpoint.notifierInfo' items must be objects");
      return false;
    }

    KjNode* kP = kjLookup(kvP, "key");
    KjNode* vP = kjLookup(kvP, "value");

    if (kP == NULL || kP->type != KjString || vP == NULL || vP->type != KjString)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
              "'notification.endpoint.notifierInfo' items must be {key:string, value:string}");
      return false;
    }

    if (strcasecmp(kP->value.s, "MQTT-QoS") == 0)
    {
      if (strcmp(vP->value.s, "0") != 0 &&
          strcmp(vP->value.s, "1") != 0 &&
          strcmp(vP->value.s, "2") != 0)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
                "'MQTT-QoS' must be '0', '1', or '2'");
        return false;
      }
    }
    else if (strcasecmp(kP->value.s, "MQTT-Version") == 0)
    {
      if (strcasecmp(vP->value.s, "mqtt3.1.1") != 0 &&
          strcasecmp(vP->value.s, "mqtt5.0")   != 0)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
                "'MQTT-Version' must be 'mqtt3.1.1' or 'mqtt5.0'");
        return false;
      }
    }
    // Other keys: accept transparently (forward to subscriber broker as-is).
  }

  return true;
}



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
    else if (strcmp(childP->name, "notifierInfo") == 0)
    {
      if (!checkNotifierInfo(childP)) return false;
    }
    // receiverInfo — forwarded as outbound headers, validated downstream.
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
  KjNode* pickP       = NULL;
  KjNode* omitP       = NULL;

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

      // Each item must be a string. Per § 5.2.12 Table 5.2.12-1, the
      // attributes alias for pick disallows "id", "type", "scope".
      for (KjNode* attrP = childP->value.firstChildP; attrP != NULL; attrP = attrP->next)
      {
        if (attrP->type != KjString)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription", "'notification.attributes' items must be strings");
          return false;
        }
        // The @vocab-expansion may have rewritten the bare token to a
        // core JSON-LD keyword (id → @id, type → @type) or to the
        // expanded NGSI-LD IRI for `scope`. Match all forms so the
        // reserved-member rejection survives expansion.
        const char* v        = attrP->value.s;
        const char* userName = NULL;
        if      (strcmp(v, "id")    == 0 || strcmp(v, "@id")           == 0)  userName = "id";
        else if (strcmp(v, "type")  == 0 || strcmp(v, "@type")         == 0)  userName = "type";
        else if (strcmp(v, "scope") == 0 || strcmp(v, LD_VOCAB_SCOPE)  == 0)  userName = "scope";
        if (userName != NULL)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
                  "'notification.attributes' must not include '%s' (§ 5.2.12)", userName);
          return false;
        }
      }
    }
    else if (strcmp(childP->name, "pick") == 0)
    {
      DUPLICATE_CHECK(pickP, "notification.pick", childP);
      ARRAY_CHECK(childP, "Invalid Subscription", "'notification.pick' must be an array");
      EMPTY_ARRAY_CHECK(childP, "'notification.pick' must not be empty");
      for (KjNode* itemP = childP->value.firstChildP; itemP != NULL; itemP = itemP->next)
      {
        if (itemP->type != KjString)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
                  "'notification.pick' items must be strings");
          return false;
        }
      }
    }
    else if (strcmp(childP->name, "omit") == 0)
    {
      DUPLICATE_CHECK(omitP, "notification.omit", childP);
      ARRAY_CHECK(childP, "Invalid Subscription", "'notification.omit' must be an array");
      EMPTY_ARRAY_CHECK(childP, "'notification.omit' must not be empty");
      for (KjNode* itemP = childP->value.firstChildP; itemP != NULL; itemP = itemP->next)
      {
        if (itemP->type != KjString)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
                  "'notification.omit' items must be strings");
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

  // § 4.21 — pick / omit / attributes are mutually exclusive.
  // attributes is the deprecated alias for pick; combining either with
  // attributes is BadRequestData.
  if (attributesP != NULL && pickP != NULL)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
            "'notification.pick' and 'notification.attributes' are mutually exclusive");
    return false;
  }
  if (attributesP != NULL && omitP != NULL)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
            "'notification.omit' and 'notification.attributes' are mutually exclusive");
    return false;
  }

  // pick + omit may co-occur, but no member may appear in both.
  if (pickP != NULL && omitP != NULL)
  {
    for (KjNode* pP = pickP->value.firstChildP; pP != NULL; pP = pP->next)
    {
      for (KjNode* oP = omitP->value.firstChildP; oP != NULL; oP = oP->next)
      {
        if (strcmp(pP->value.s, oP->value.s) == 0)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
                  "'notification.pick' and 'notification.omit' must not share member '%s'",
                  pP->value.s);
          return false;
        }
      }
    }
  }

  MANDATORY_CHECK(endpointP, "Invalid Subscription", "'notification.endpoint' is mandatory");

  if (checkEndpoint(endpointP) == false)
    return false;

  return true;
}



// -----------------------------------------------------------------------------
//
// ldSubEntityTypeExprsRelease - free the per-request entity-type-expr scratch
//
// checkEntitiesArray parses one LdTypeExpr per "entities" entry into the
// per-thread swNgsild.subEntityTypeExprsV side-channel; ldSubCacheItemAdd then
// TRANSFERS each to its cache item (zeroing the slot). Whatever is left (the
// container array, plus any untransferred expr) must be released. Previously
// this happened only on the NEXT checkEntitiesArray call, so the last buffer
// per worker thread leaked at exit. The broker now calls this from the
// post-response hook so the scratch is freed per request. Idempotent.
//
void ldSubEntityTypeExprsRelease(void)
{
  if (swNgsild.subEntityTypeExprsV == NULL)
    return;

  for (int i = 0; i < swNgsild.subEntityTypeExprsN; i++)
    ldTypeExprFree(swNgsild.subEntityTypeExprsV[i]);

  free(swNgsild.subEntityTypeExprsV);
  swNgsild.subEntityTypeExprsV = NULL;
  swNgsild.subEntityTypeExprsN = 0;
}



// -----------------------------------------------------------------------------
//
// checkEntitiesArray - validate the "entities" array
//
static bool checkEntitiesArray(KjNode* entitiesP)
{
  ARRAY_CHECK(entitiesP, "Invalid Subscription", "'entities' must be a JSON array");
  EMPTY_ARRAY_CHECK(entitiesP, "'entities' must not be empty");

  // § 4.17 — parse each entities[].type into a malloc-allocated
  // LdTypeExpr and stash on swNgsild for the sub-cache to claim.
  // Allocate the side-channel sized to the array length; one slot
  // per entry, NULL slot = no type field. Old contents (e.g. left
  // over from a previous request on the same thread) are released
  // here.
  int entCount = 0;
  for (KjNode* p = entitiesP->value.firstChildP; p != NULL; p = p->next)
    entCount++;

  ldSubEntityTypeExprsRelease();   // drop any leftover from a prior request on this thread
  if (entCount > 0)
  {
    swNgsild.subEntityTypeExprsV = (LdTypeExpr**) calloc(entCount, sizeof(LdTypeExpr*));
    swNgsild.subEntityTypeExprsN = entCount;
  }

  int entIx = 0;
  for (KjNode* entP = entitiesP->value.firstChildP; entP != NULL; entP = entP->next, entIx++)
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

        // § 4.17 — a bare "*" is the match-any wildcard: it imposes no type
        // constraint. Represent it as a NULL typeExpr (the sub-cache selector
        // then matches any entity type, like a selector with no type at all).
        // The raw "*" stays in the subscription tree for round-trip on GET.
        if (strcmp(fieldP->value.s, "*") == 0)
        {
          swNgsild.subEntityTypeExprsV[entIx] = NULL;
        }
        else
        {
          // Parse the §4.17 expression up front. NULL allocator → malloc;
          // tree outlives this request and is handed off to the sub cache.
          LdTypeExpr* expr = ldTypeExprParse(fieldP->value.s, NULL);
          if (expr == NULL)
            return false;  // ldError already set inside the parser
          swNgsild.subEntityTypeExprsV[entIx] = expr;
        }
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
static bool checkGeoQ(KjNode* geoQP, KAlloc* kaP)
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

  //
  // geometry and coordinates must be structurally consistent. Without this a
  // Polygon geometry paired with Point-shaped coordinates (or a malformed ring)
  // is stored as an unusable geoQ that silently matches nothing — leaving the
  // subscription in an inconsistent state. § 5.2.13: coordinates is "a JSON
  // Array coherent with the geometry type" (and MAY be string-encoded for
  // JSON-LD compatibility). Reuse the full structural validator (range, ring
  // closure, ...), which wants the coordinates as a JSON string.
  //
  const char* coordsStr = NULL;

  if (coordinatesP->type == KjString)
    coordsStr = coordinatesP->value.s;
  else  // KjArray (the primary form) — render to the JSON string the validator parses
  {
    // kjFastRender omits the top-level node's name, so this emits the bare array
    // (not "coordinates":[...]) — a top-level JSON array kjParse can re-parse.
    int   len = kjFastRenderSize(coordinatesP) + 1;
    char* buf = (char*) kaAlloc(kaP, len);
    if (buf != NULL)
    {
      kjFastRender(coordinatesP, buf);
      coordsStr = buf;
    }
  }

  if (coordsStr == NULL)
    return true;  // could not materialize coordinates; nothing further to check

  if (ldCheckGeoQuery(geometryP->value.s, coordsStr) == false)
    return false;  // ldError already set by ldCheckGeoQuery

  //
  // Normalize the string-encoded form to the GeoJSON array (§ 5.2.13 primary
  // form) IN THE STORED TREE, so the persisted subscription is canonical
  // regardless of which form the client sent — and so a cache rebuild after a
  // restart reads the same array. Parse into kaP (request lifetime, same as the
  // subscription tree); the parsed array's children replace the string node in
  // place (name + sibling links preserved).
  //
  if (coordinatesP->type == KjString)
  {
    char* dup = kaStrdup(kaP, coordsStr);  // kjParse mutates its input
    if (dup != NULL)
    {
      Kjson   kjson;
      Kjson*  kjsonP = kjBufferCreate(&kjson, kaP);
      KjNode* arrP   = kjParse(kjsonP, dup);

      if (arrP != NULL && arrP->type == KjArray)
      {
        coordinatesP->type              = KjArray;
        coordinatesP->value.firstChildP = arrP->value.firstChildP;
        coordinatesP->lastChild         = arrP->lastChild;
      }
    }
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

  // § 5.2 Subscription table: `notificationTrigger` "is not applicable and
  // shall be ignored if the Subscription data type is used for a Context
  // Source Registration Subscription". Strip silently on the CSR-sub paths
  // (create + update) so it never reaches storage. Entity-sub paths keep
  // the field — that's where it's meaningful.
  if (op == LdOpCreateCsourceSubscription || op == LdOpUpdateCsourceSubscription)
  {
    KjNode* ntP = kjLookup(subP, "notificationTrigger");
    if (ntP != NULL)
      kjChildRemove(subP, ntP);
  }

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

    //
    // Delete-markers (TS 104-175 clause-8 / § 5.8.3) — see ldCheckRegistration
    // for the rationale. On the update path the "urn:ngsi-ld:null" sentinel
    // deletes the member (→ internal KjNull → DB plugin $unset); raw JSON null
    // is rejected; 'notification' is mandatory and cannot be deleted. On create
    // the sentinel is not allowed as a first-level value.
    //
    if (op == LdOpUpdateSubscription || op == LdOpUpdateCsourceSubscription)
    {
      if (childP->type == KjNull)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value",
                "JSON null is not allowed in NGSI-LD; use the 'urn:ngsi-ld:null' delete-marker (field: '%s')", name);
        return false;
      }
      if (childP->type == KjString && strcmp(childP->value.s, LD_VOCAB_NGSILD_NULL) == 0)
      {
        if (strcmp(name, LD_VOCAB_NOTIFICATION) == 0)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
                  "'notification' is mandatory and cannot be deleted");
          return false;
        }
        childP->type = KjNull;   // internal delete signal honoured by the DB plugin ($unset)
        continue;
      }
    }
    else if (childP->type == KjString && strcmp(childP->value.s, LD_VOCAB_NGSILD_NULL) == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value",
              "'urn:ngsi-ld:null' is not allowed as a first-level value in Create Subscription (field: '%s')", name);
      return false;
    }

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
      // § 5.2.12 — jsonldContext is a URL from which the broker shall
      // retrieve the JSON-LD @context, so it must be dereferenceable —
      // http:// or https:// only. A URN, a bare short name, or anything
      // else is BadRequestData.
      STRING_CHECK(childP, "Invalid Subscription", "'jsonldContext' must be a string");
      const char* s = childP->value.s;
      if (strncmp(s, "http://", 7) != 0 && strncmp(s, "https://", 8) != 0)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
                "'jsonldContext' must be an http(s) URL: '%s'", s);
        return false;
      }
    }
    else if (strcmp(name, "ngsildConformance") == 0)
    {
      // § 5.2.12 / § 4.3.6.8: backwards-compat target. Format "M.m".
      STRING_CHECK(childP, "Invalid Subscription", "'ngsildConformance' must be a string");
      short m, n;
      if (!ldConformanceParse(childP->value.s, &m, &n))
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
                "'ngsildConformance' must be a 'major.minor' version string (got '%s')",
                childP->value.s);
        return false;
      }
    }
  }

  //
  // `type` is validated AND stripped by ldParseHook for these URLs — see
  // swNgsild.fixedTypeRecord. Nothing to check here.
  //
  (void) typeP;

  //
  // "id" — when present, must be a valid URI. Spec § 5.2.12 (Subscription)
  // says id is a "Valid URI" (mandatory on create, immutable on update).
  // Without this check the broker accepts garbage like ?id="invalidId"
  // (ETSI 028_03_01 / 028_03_02 — InvalidId / EmptyId).
  //
  if (idP != NULL)
  {
    STRING_CHECK(idP, "Invalid Subscription", "'id' must be a string");
    if (idP->value.s[0] == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription", "'id' must not be empty");
      return false;
    }
    URI_CHECK(idP->value.s);
  }

  //
  // PATCH bodies that include id are tolerated when the id matches the
  // URL — but we don't have the URL id here without weaving in
  // swRest.in.wildcard[0]. For now keep the strict reject on update
  // (matches prior behaviour); ETSI 029 doesn't exercise this path.
  //
  if ((op == LdOpUpdateSubscription || op == LdOpUpdateCsourceSubscription) && idP != NULL)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Read-Only Field", "'id' cannot be modified");
    return false;
  }

  //
  // "entities" or "watchedAttributes" required for create
  //
  if (op == LdOpCreateSubscription || op == LdOpCreateCsourceSubscription)
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
  if (op == LdOpCreateSubscription || op == LdOpCreateCsourceSubscription)
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
    STRING_CHECK(expiresAtP, "Invalid DateTime", "'expiresAt' must be a DateTime string");
    double expiresAtSec;
    if (!ldCheckDateTime(expiresAtP->value.s, &expiresAtSec))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid DateTime",
              "'expiresAt' is not a valid ISO 8601 DateTime");
      return false;
    }

    // § 5.2.12 — at create time, expiresAt must be in the future.
    // Update is allowed to set a past timestamp (used to expire-immediately).
    if (op == LdOpCreateSubscription || op == LdOpCreateCsourceSubscription)
    {
      if (expiresAtSec <= (double) time(NULL))
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Subscription",
                "'expiresAt' must be in the future");
        return false;
      }
    }
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
  // "q" must be a string holding a syntactically valid query (§ 5.2.12:
  // "q ... as per clause 4.9"). ldQParse raises the 400 ProblemDetails
  // itself on a syntax error; the parse tree is thrown away — the matcher
  // compiles its own at notification time.
  //
  if (qP != NULL)
  {
    STRING_CHECK(qP, "Invalid Subscription", "'q' must be a string");
    if (ldQParse(qP->value.s, kaP) == NULL)
      return false;
  }

  //
  // "geoQ" validation
  //
  if (geoQP != NULL)
  {
    if (checkGeoQ(geoQP, kaP) == false)
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
