//
// FILE            ldNormalizeInput.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
// Converts simplified and concise NGSI-LD input to normalized format.
// Called after JSON-LD expansion, before validation and DB transform.
//
// Simplified:  "speed": 100                           -> { "type": "Property", "hasValue": 100 }
// Concise:     "speed": { "value": 100 }              -> { "type": "Property", "value": 100 }   (add type)
// Normalized:  "speed": { "type": "Property", ... }   -> unchanged
//
#include <stdbool.h>                                     // bool
#include "swRest/swRest.h"                            // swRest
#include <string.h>                                      // strcmp

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjBuilder.h"                             // kjObject
#include "kjson/kjChildReplace.h"                       // kjChildReplace

#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/LdAttrType.h"                         // LdAttrType
#include "swNgsild/ldTypes.h"                             // ldAttrTypeToString
#include "swNgsild/ldAttrTypeDetect.h"                   // ldAttrTypeDetect
#include "swNgsild/ldIsEntityKeyword.h"                   // ldIsEntityKeyword
#include "swNgsild/LdNormalizeInput.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// isAttrKeyword - attribute-level keywords that are NOT sub-attributes
//
static bool isAttrKeyword(const char* name)
{
  if (strcmp(name, "type")                    == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_VALUE)        == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_OBJECT)       == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_LANGUAGE_MAP) == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_VOCAB)        == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_VALUE_LIST)   == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_OBJECT_LIST)  == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_JSON)         == 0)  return true;
  if (strcmp(name, LD_VOCAB_OBSERVED_AT)      == 0)  return true;
  if (strcmp(name, LD_VOCAB_UNIT_CODE)        == 0)  return true;
  if (strcmp(name, LD_VOCAB_DATASET_ID)       == 0)  return true;

  return false;
}



// -----------------------------------------------------------------------------
//
// hasValueKey - check if an object has any NGSI-LD value key
//
static bool hasValueKey(KjNode* objP)
{
  for (KjNode* childP = objP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, LD_VOCAB_HAS_VALUE)        == 0)  return true;
    if (strcmp(childP->name, LD_VOCAB_HAS_OBJECT)       == 0)  return true;
    if (strcmp(childP->name, LD_VOCAB_HAS_LANGUAGE_MAP) == 0)  return true;
    if (strcmp(childP->name, LD_VOCAB_HAS_VOCAB)        == 0)  return true;
    if (strcmp(childP->name, LD_VOCAB_HAS_VALUE_LIST)   == 0)  return true;
    if (strcmp(childP->name, LD_VOCAB_HAS_OBJECT_LIST)  == 0)  return true;
    if (strcmp(childP->name, LD_VOCAB_HAS_JSON)         == 0)  return true;
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// isGeoJsonTypeName - check if a string is a GeoJSON geometry type name
//
static bool isGeoJsonTypeName(const char* s)
{
  if (strcmp(s, "Point")              == 0)  return true;
  if (strcmp(s, "LineString")         == 0)  return true;
  if (strcmp(s, "Polygon")            == 0)  return true;
  if (strcmp(s, "MultiPoint")         == 0)  return true;
  if (strcmp(s, "MultiLineString")    == 0)  return true;
  if (strcmp(s, "MultiPolygon")       == 0)  return true;
  if (strcmp(s, LD_VOCAB_GEO_POINT)         == 0)  return true;
  if (strcmp(s, LD_VOCAB_GEO_LINE_STRING)   == 0)  return true;
  if (strcmp(s, LD_VOCAB_GEO_POLYGON)       == 0)  return true;
  if (strcmp(s, LD_VOCAB_GEO_MULTI_POINT)   == 0)  return true;
  if (strcmp(s, LD_VOCAB_GEO_MULTI_LINE)    == 0)  return true;
  if (strcmp(s, LD_VOCAB_GEO_MULTI_POLYGON) == 0)  return true;

  return false;
}



// -----------------------------------------------------------------------------
//
// isGeoJsonObject - check if an KjObject looks like a GeoJSON geometry
//
// A GeoJSON geometry has:
//   - A "type" child with a GeoJSON geometry type name
//   - A coordinates child (LD_VOCAB_COORDINATES)
//   - No NGSI-LD value keys (hasValue, hasObject, etc.)
//
static bool isGeoJsonObject(KjNode* objP)
{
  bool        hasGeoType   = false;
  bool        hasCoords    = false;

  for (KjNode* childP = objP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, "type") == 0 && childP->type == KjString && isGeoJsonTypeName(childP->value.s))
      hasGeoType = true;
    else if (strcmp(childP->name, LD_VOCAB_COORDINATES) == 0)
      hasCoords = true;
  }

  return (hasGeoType && hasCoords && !hasValueKey(objP));
}



// -----------------------------------------------------------------------------
//
// isGeoJsonValue - check if a value node (child of hasValue) is GeoJSON
//
static bool isGeoJsonValue(KjNode* valueP)
{
  if (valueP->type != KjObject)
    return false;

  return isGeoJsonObject(valueP);
}



// -----------------------------------------------------------------------------
//
// hasExplicitAttrType - check if the object already has an NGSI-LD "type" field
//                       with a known attribute type value
//
static bool hasExplicitAttrType(KjNode* objP)
{
  for (KjNode* childP = objP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, "type") == 0 && childP->type == KjString)
    {
      const char* v = childP->value.s;
      if (strcmp(v, "Property")         == 0)  return true;
      if (strcmp(v, "Relationship")     == 0)  return true;
      if (strcmp(v, "GeoProperty")      == 0)  return true;
      if (strcmp(v, "LanguageProperty") == 0)  return true;
      if (strcmp(v, "VocabProperty")    == 0)  return true;
      if (strcmp(v, "ListProperty")     == 0)  return true;
      if (strcmp(v, "ListRelationship") == 0)  return true;
      if (strcmp(v, "JsonProperty")     == 0)  return true;
      // Also accept expanded IRIs
      if (strncmp(v, "https://uri.etsi.org/ngsi-ld/", 29) == 0)  return true;
    }
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// addTypeField - prepend a "type" string field to an attribute object
//
static void addTypeField(KjNode* attrP, const char* typeName, KAlloc* faP)
{
  KjNode* typeNodeP = kjString(swRest.kjsonP, "type", typeName);
  if (typeNodeP != NULL)
  {
    typeNodeP->next = attrP->value.firstChildP;
    attrP->value.firstChildP = typeNodeP;
  }
}



// -----------------------------------------------------------------------------
//
// wrapAsProperty - wrap a scalar/array node inside a Property object
//
// Input:   entityP has child  "attrName": <value>
// Output:  entityP has child  "attrName": { "type": "Property", LD_VOCAB_HAS_VALUE: <value> }
//
static void wrapAsProperty(KjNode* entityP, KjNode* childP, KAlloc* faP)
{
  KjNode* wrapperP = kjObject(swRest.kjsonP, childP->name);
  if (wrapperP == NULL)
    return;

  // Create a value node that copies the original's type+value
  KjNode* valueNodeP = (KjNode*) kaAlloc(faP, sizeof(KjNode));
  if (valueNodeP == NULL)
    return;

  memset(valueNodeP, 0, sizeof(KjNode));
  valueNodeP->name  = (char*) LD_VOCAB_HAS_VALUE;
  valueNodeP->type  = childP->type;
  valueNodeP->value = childP->value;
  valueNodeP->next  = NULL;

  kjChildAdd(wrapperP, valueNodeP);
  addTypeField(wrapperP, "Property", faP);

  kjChildReplace(entityP, childP, wrapperP);
}



// -----------------------------------------------------------------------------
//
// wrapAsGeoProperty - wrap a GeoJSON object inside a GeoProperty
//
static void wrapAsGeoProperty(KjNode* entityP, KjNode* childP, KAlloc* faP)
{
  KjNode* wrapperP = kjObject(swRest.kjsonP, childP->name);
  if (wrapperP == NULL)
    return;

  // Create hasValue node pointing to the GeoJSON object's children
  KjNode* valueNodeP = (KjNode*) kaAlloc(faP, sizeof(KjNode));
  if (valueNodeP == NULL)
    return;

  memset(valueNodeP, 0, sizeof(KjNode));
  valueNodeP->name  = (char*) LD_VOCAB_HAS_VALUE;
  valueNodeP->type  = childP->type;
  valueNodeP->value = childP->value;
  valueNodeP->next  = NULL;

  kjChildAdd(wrapperP, valueNodeP);
  addTypeField(wrapperP, "GeoProperty", faP);

  kjChildReplace(entityP, childP, wrapperP);
}



static void normalizeAttr(KjNode* containerP, KjNode* attrP, KAlloc* faP, bool mergeMode);



// -----------------------------------------------------------------------------
//
// normalizeAttr - normalize a single attribute (may be object, scalar, or array)
//
// containerP: the parent node (entity or attribute object) — needed for kjChildReplace
// attrP:      the attribute node to normalize
//
static void normalizeAttr(KjNode* containerP, KjNode* attrP, KAlloc* faP, bool mergeMode)
{
  // ---  Scalar children → simplified Property  ---
  if (attrP->type == KjInt || attrP->type == KjFloat || attrP->type == KjString || attrP->type == KjBoolean || attrP->type == KjNull)
  {
    // Leave NGSI-LD null delete-markers alone: they are never a Property value.
    // In Merge Entity (PATCH § 5.6.17) they indicate deletion of the named
    // (sub-)attribute; in Create Entity they are rejected later by ldCheckEntity.
    if (attrP->type == KjString && strcmp(attrP->value.s, LD_VOCAB_NGSILD_NULL) == 0)
      return;

    // In merge mode (PATCH), leave simplified scalars untouched. ldEntityMerge
    // resolves them against the target's existing attribute type so that e.g.
    // a scalar in the fragment updates the Relationship's object, the
    // LanguageProperty's languageMap[<lang>], or the Property's value, without
    // changing the attribute's type.
    if (mergeMode)
      return;

    wrapAsProperty(containerP, attrP, faP);
    return;
  }

  // ---  Array children  ---
  if (attrP->type == KjArray)
  {
    KjNode* firstP = attrP->value.firstChildP;

    if (firstP == NULL)
      return;  // empty array — leave for ldCheckEntity to reject

    if (firstP->type == KjObject)
    {
      // Multi-attribute array — normalize each element
      KjNode* elemP = firstP;
      while (elemP != NULL)
      {
        KjNode* elemNextP = elemP->next;
        if (elemP->type == KjObject)
          normalizeAttr(attrP, elemP, faP, mergeMode);
        elemP = elemNextP;
      }
    }
    else
    {
      // Simplified array value (e.g. "tags": ["fast", "red"])
      wrapAsProperty(containerP, attrP, faP);
    }
    return;
  }

  // ---  Object children  ---
  if (attrP->type != KjObject)
    return;

  // Case 1: Has explicit attr type (Property/Relationship/etc.) → already normalized or concise with type
  if (hasExplicitAttrType(attrP))
  {
    // Recurse into sub-attributes
    KjNode* subP = attrP->value.firstChildP;
    while (subP != NULL)
    {
      KjNode* subNextP = subP->next;
      if (isAttrKeyword(subP->name) == false)
        normalizeAttr(attrP, subP, faP, mergeMode);
      subP = subNextP;
    }
    return;
  }

  // Case 2: Has a value key but no explicit attr type → concise format
  if (hasValueKey(attrP))
  {
    // Need to detect type and add it
    // Special case: hasValue with GeoJSON value → GeoProperty
    KjNode* hasValueP = NULL;
    for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
    {
      if (strcmp(childP->name, LD_VOCAB_HAS_VALUE) == 0)
      {
        hasValueP = childP;
        break;
      }
    }

    if (hasValueP != NULL && isGeoJsonValue(hasValueP))
    {
      addTypeField(attrP, "GeoProperty", faP);
    }
    else
    {
      LdAttrType detectedType = ldAttrTypeDetect(attrP);
      if (detectedType != LdAttrNone)
        addTypeField(attrP, ldAttrTypeToString(detectedType), faP);
    }

    // Recurse into sub-attributes
    KjNode* subP = attrP->value.firstChildP;
    while (subP != NULL)
    {
      KjNode* subNextP = subP->next;
      if (isAttrKeyword(subP->name) == false)
        normalizeAttr(attrP, subP, faP, mergeMode);
      subP = subNextP;
    }
    return;
  }

  // Case 3: Object with no value key, no attr type
  // Check if it IS a GeoJSON geometry (simplified GeoProperty)
  if (isGeoJsonObject(attrP))
  {
    wrapAsGeoProperty(containerP, attrP, faP);
    return;
  }

  // Case 4: Plain object value → simplified Property
  //
  // In merge mode (PATCH § 5.6.17), a fragment attribute that is a plain
  // object without type or value key is a partial attribute fragment whose
  // children are sub-attributes to be merged into an existing attribute. Do
  // not wrap it — recurse into the sub-attributes so they get normalized in
  // their own right.
  //
  if (mergeMode)
  {
    KjNode* subP = attrP->value.firstChildP;
    while (subP != NULL)
    {
      KjNode* subNextP = subP->next;
      if (isAttrKeyword(subP->name) == false)
        normalizeAttr(attrP, subP, faP, mergeMode);
      subP = subNextP;
    }
    return;
  }

  wrapAsProperty(containerP, attrP, faP);
}



// -----------------------------------------------------------------------------
//
// ldNormalizeInput -
//
void ldNormalizeInput(KjNode* entityP, KAlloc* faP, bool mergeMode)
{
  if (entityP == NULL || entityP->type != KjObject)
    return;

  KjNode* childP = entityP->value.firstChildP;

  while (childP != NULL)
  {
    KjNode* nextP = childP->next;  // save before normalizeAttr may replace childP

    if (ldIsEntityKeyword(childP->name) == false)
      normalizeAttr(entityP, childP, faP, mergeMode);

    childP = nextP;
  }
}
