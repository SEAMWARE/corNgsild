//
// FILE            ldPCheckQuery.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Payload-body validator for POST /ngsi-ld/v1/entityOperations/query (§ 5.2.23
// Query). Run as the route's 'payloadCheck' hook before the service routine.
// The handler (ldQueryBodyToParams) is permissive — it extracts the filters it
// recognises and forwards the rest, so a malformed body would slip through as a
// 200. This validator rejects each malformed shape up front with a pinpointed
// 400, before the service routine runs (registered as the route's payloadCheck
// hook), so nothing malformed reaches the query handler.
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strcmp

#include "swRest/SwRestState.h"                          // swRest
#include "kjson/KjNode.h"                                // KjNode
#include "kjson/kjLookup.h"                              // kjLookup

#include "swNgsild/swNgsild.h"                           // ldError, LD_ERROR_*
#include "swNgsild/ldQParse.h"                           // ldQParse
#include "swNgsild/ldCheckUri.h"                         // ldCheckUri
#include "swNgsild/LdGeoRel.h"                           // ldGeoRelParse
#include "swNgsild/ldPCheckQuery.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// Field descriptor flags
//
#define PC_MANDATORY  0x01



// -----------------------------------------------------------------------------
//
// LdField - one entry of a payload-body field descriptor table
//
// 'typeMask' is a bitmask of allowed KjValueType values (1 << KjString, ...).
// 'nodeP' is filled in by ldFieldsExtract with the matched body node (NULL if
// the field is absent) — also used to detect duplicates.
//
typedef struct LdField
{
  const char*  name;
  int          typeMask;
  int          flags;
  KjNode*      nodeP;
} LdField;



// -----------------------------------------------------------------------------
//
// typeTitle - "Not a JSON Xxx" title for a single-type field
//
static const char* typeTitle(int typeMask)
{
  if (typeMask == (1 << KjString))   return "Not a JSON String";
  if (typeMask == (1 << KjArray))    return "Not a JSON Array";
  if (typeMask == (1 << KjObject))   return "Not a JSON Object";
  if (typeMask == (1 << KjBoolean))  return "Not a JSON Boolean";
  return "Invalid JSON type";
}



// -----------------------------------------------------------------------------
//
// ldFieldsExtract - generic first-level validation of an object's members
//
// Detects: unknown field, duplicated field, wrong JSON type, empty array/object,
// empty string. Then checks for mandatory-but-missing. On the first violation it
// sets the ProblemDetails (ldError) and returns false; on success every present
// field is recorded in its descriptor's 'nodeP'.
//
static bool ldFieldsExtract(KjNode* objP, LdField* fieldV, int fields, bool errorOnUnknown)
{
  for (KjNode* nodeP = objP->value.firstChildP; nodeP != NULL; nodeP = nodeP->next)
  {
    if (nodeP->name == NULL || nodeP->name[0] == '@')   // skip @context et al.
      continue;

    LdField* fP = NULL;
    for (int ix = 0; ix < fields; ix++)
    {
      if (strcmp(fieldV[ix].name, nodeP->name) == 0)
      {
        fP = &fieldV[ix];
        break;
      }
    }

    if (fP == NULL)
    {
      if (errorOnUnknown == true)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid field for query", "%s", nodeP->name);
        return false;
      }
      continue;
    }

    // Duplicate fields are rejected uniformly by the duplicate-member check in
    // ldParseHook, before this validation runs.
    if (((1 << nodeP->type) & fP->typeMask) == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, typeTitle(fP->typeMask), "%s", nodeP->name);
      return false;
    }
    if ((nodeP->type == KjArray || nodeP->type == KjObject) && nodeP->value.firstChildP == NULL)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, (nodeP->type == KjArray) ? "Empty Array" : "Empty Object", "%s", nodeP->name);
      return false;
    }
    if (nodeP->type == KjString && nodeP->value.s[0] == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Empty String", "%s", nodeP->name);
      return false;
    }

    fP->nodeP = nodeP;
  }

  for (int ix = 0; ix < fields; ix++)
  {
    if ((fieldV[ix].flags & PC_MANDATORY) && fieldV[ix].nodeP == NULL)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Mandatory field missing", "%s", fieldV[ix].name);
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// findField - the body node for a descriptor field by name (NULL if absent)
//
static KjNode* findField(LdField* fieldV, int fields, const char* name)
{
  for (int ix = 0; ix < fields; ix++)
    if (strcmp(fieldV[ix].name, name) == 0)
      return fieldV[ix].nodeP;
  return NULL;
}



// -----------------------------------------------------------------------------
//
// pCheckEntities - each member of the Query 'entities' array is a valid selector
//
static bool pCheckEntities(KjNode* entitiesP)
{
  for (KjNode* entityP = entitiesP->value.firstChildP; entityP != NULL; entityP = entityP->next)
  {
    if (entityP->type != KjObject)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Not a JSON Object", "entities array member");
      return false;
    }

    LdField selV[] =
    {
      { "id",        (1 << KjString), 0,            NULL },
      { "idPattern", (1 << KjString), 0,            NULL },
      { "type",      (1 << KjString), PC_MANDATORY, NULL }
    };

    if (ldFieldsExtract(entityP, selV, 3, true) == false)
      return false;

    // § 5.2.7: the selector 'id' is an Entity id — must be a URI.
    if (selV[0].nodeP != NULL && ldCheckUri(selV[0].nodeP->value.s) == false)
      return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// pCheckGeoQ - validate the Query 'geoQ' (§ 5.2.13 GeoQuery) sub-object
//
// geometry, coordinates and georel are mandatory; geoproperty is optional. The
// geometry-type and coordinates-shape values are deep-checked by the handler's
// URL-param path (ldParamHook) — here we own the structure (mandatory/type/
// unknown/duplicate) plus the georel parse, which the handler does not check.
//
static bool pCheckGeoQ(KjNode* geoQP)
{
  LdField geoV[] =
  {
    { "geometry",    (1 << KjString),                  PC_MANDATORY, NULL },
    { "coordinates", (1 << KjString) | (1 << KjArray), PC_MANDATORY, NULL },
    { "georel",      (1 << KjString),                  PC_MANDATORY, NULL },
    { "geoproperty", (1 << KjString),                  0,            NULL }
  };

  if (ldFieldsExtract(geoQP, geoV, 4, true) == false)
    return false;

  // georel syntax (near;maxDistance==N, within, ...) — ldGeoRelParse sets its
  // own ProblemDetails; add a fallback in case it returns NULL silently.
  if (ldGeoRelParse(geoV[2].nodeP->value.s, &swRest.kalloc) == NULL)
  {
    if (swRest.out.problemType == NULL)
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid georel", "%s", geoV[2].nodeP->value.s);
    return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// pCheckUriArray - every member of a URI array (e.g. datasetId) is a URI
//
// The reserved keyword '@none' (instances without the field) is exempt.
//
static bool pCheckUriArray(KjNode* arrayP, const char* what)
{
  for (KjNode* memberP = arrayP->value.firstChildP; memberP != NULL; memberP = memberP->next)
  {
    if (memberP->type != KjString)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Not a JSON String", "%s array member", what);
      return false;
    }
    if (strcmp(memberP->value.s, "@none") == 0)
      continue;
    if (ldCheckUri(memberP->value.s) == false)
      return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// pCheckAttrs - each member of the Query 'attrs' array is a non-empty string
//
static bool pCheckAttrs(KjNode* attrsP)
{
  for (KjNode* attrP = attrsP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    if (attrP->type != KjString)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Not a JSON String", "attrs array member");
      return false;
    }
    if (attrP->value.s[0] == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Empty String", "attrs array member");
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// pCheckQuery -
//
bool pCheckQuery(void)
{
  KjNode* bodyP = swRest.in.requestTree;

  if (bodyP == NULL)                                     // dispatcher already 400s an empty POST body
    return true;

  if (bodyP->type != KjObject)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Not a JSON Object", "the POST Query payload body must be a JSON object");
    return false;
  }

  // § 5.2.6.5.1 Query data type — the complete member set. 'type' is the only
  // mandatory member; entities/attrs/omit/pick/containedBy/datasetId are arrays,
  // geoQ/temporalQ/aggrParams/ordering are nested objects, the rest are scalars.
  // geometryProperty and local are URL-param filters the body may mirror. Any
  // field not in this set is rejected as unknown (catches typos that would
  // otherwise silently widen the query).
  #define PC_STR  (1 << KjString)
  #define PC_ARR  (1 << KjArray)
  #define PC_OBJ  (1 << KjObject)
  #define PC_BOOL (1 << KjBoolean)
  #define PC_NUM  ((1 << KjInt) | (1 << KjFloat))
  LdField fieldV[] =
  {
    { "type",              PC_STR,  PC_MANDATORY, NULL },
    { "entities",          PC_ARR,  0,            NULL },
    { "q",                 PC_STR,  0,            NULL },
    { "geoQ",              PC_OBJ,  0,            NULL },
    { "scopeQ",            PC_STR,  0,            NULL },
    { "temporalQ",         PC_OBJ,  0,            NULL },
    { "attrs",             PC_ARR,  0,            NULL },
    { "omit",              PC_ARR,  0,            NULL },
    { "pick",              PC_ARR,  0,            NULL },
    { "aggrParams",        PC_OBJ,  0,            NULL },
    { "csf",               PC_STR,  0,            NULL },
    { "containedBy",       PC_ARR,  0,            NULL },
    { "createEntityMap",   PC_BOOL, 0,            NULL },
    { "datasetId",         PC_ARR,  0,            NULL },
    { "expandValues",      PC_STR,  0,            NULL },
    { "entityMapLifetime", PC_STR,  0,            NULL },
    { "jsonKeys",          PC_STR,  0,            NULL },
    { "join",              PC_STR,  0,            NULL },
    { "joinLevel",         PC_NUM,  0,            NULL },
    { "lang",              PC_STR,  0,            NULL },
    { "ordering",          PC_OBJ,  0,            NULL },
    { "splitEntities",     PC_BOOL, 0,            NULL },
    { "geometryProperty",  PC_STR,  0,            NULL },
    { "local",             PC_BOOL, 0,            NULL }
  };
  int fields = sizeof(fieldV) / sizeof(fieldV[0]);

  if (ldFieldsExtract(bodyP, fieldV, fields, true) == false)
    return false;

  // type must be the string "Query" (the parseHook may have expanded it to the
  // default-vocab IRI — accept both, matching ldQueryBodyToParams).
  const char* typeS = fieldV[0].nodeP->value.s;
  if (strcmp(typeS, "Query") != 0 &&
      strcmp(typeS, "https://uri.etsi.org/ngsi-ld/default-context/Query") != 0)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid value for 'type' member of a POST Query",
            "must be a JSON String with the value 'Query'");
    return false;
  }

  KjNode* entitiesP  = findField(fieldV, fields, "entities");
  KjNode* attrsP     = findField(fieldV, fields, "attrs");
  KjNode* qP         = findField(fieldV, fields, "q");
  KjNode* geoQP      = findField(fieldV, fields, "geoQ");
  KjNode* datasetIdP = findField(fieldV, fields, "datasetId");

  if (entitiesP != NULL && pCheckEntities(entitiesP) == false)  return false;
  if (attrsP    != NULL && pCheckAttrs(attrsP)        == false)  return false;

  // q-expression: reject a syntactically broken filter up front (ldQParse sets
  // its own ProblemDetails; add a fallback in case it returns NULL silently).
  if (qP != NULL)
  {
    if (ldQParse(qP->value.s, &swRest.kalloc) == NULL)
    {
      if (swRest.out.problemType == NULL)
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Parse Error in q-expression", "%s", qP->value.s);
      return false;
    }
  }

  if (geoQP      != NULL && pCheckGeoQ(geoQP)                       == false)  return false;
  if (datasetIdP != NULL && pCheckUriArray(datasetIdP, "datasetId") == false)  return false;

  return true;
}
