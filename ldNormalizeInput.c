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

#include "swJsonld/swldExpand.h"                          // KJF_CORE_TERM
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/LdAttrType.h"                         // LdAttrType
#include "swNgsild/ldTypes.h"                             // ldAttrTypeToString
#include "swNgsild/ldAttrTypeDetect.h"                   // ldAttrTypeDetect
#include "swNgsild/ldIsEntityKeyword.h"                   // ldIsEntityKeyword
#include "swNgsild/LdNormalizeInput.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// isAttrKeyword - is this node a structural attribute member (NOT a sub-attribute)?
//
// The specific structural members (type, value, object, languageMap, vocab,
// valueList, objectList, json, observedAt, expiresAt, unitCode, datasetId,
// valueType) are classified once at core-context load and the KJF_ATTR_TERM bit
// is copied onto each matching node during swldExpandTree. ldNormalizeInput runs
// only after swldExpandTree (ldHooks), so the bit is set by the time we look.
// One bit test instead of a strcmp chain — and it picks up valueType, which the
// old list omitted (causing valueType to be wrongly reified as a sub-Property).
// Note: a sub-attribute may itself be a core-context term, so this tests the
// specific KJF_ATTR_TERM marking, not a generic "from core context" flag.
//
static bool isAttrKeyword(const KjNode* nodeP)
{
  return ((nodeP->flags & KJF_ATTR_TERM) != 0);
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
// isSimplifiedGeoProperty - structural test for a simplified-format GeoProperty
//
// An attribute object whose ONLY members are exactly "type" (a string) and
// "coordinates" is a simplified-format GeoProperty (a bare GeoJSON geometry).
// Unlike isGeoJsonObject(), the "type" value need NOT be a valid GeoJSON
// geometry name: an invalid one (e.g. "Poinxt") must still be recognized as a
// GeoProperty *attempt* so it gets wrapped and ldCheckGeo rejects it as a bad
// geometry — rather than the whole object being stored as a junk Property.
//
// Only reached from Case 3 (no explicit attr type, no value key), so a valid
// NGSI-LD attribute type such as "Property" has already been handled as
// normalized format by Case 1 ({"type":"Property","coordinates":[]} → "Missing
// value", not a geometry).
//
static bool isSimplifiedGeoProperty(KjNode* objP)
{
  KjNode*  typeP   = NULL;
  KjNode*  coordsP = NULL;
  int      count   = 0;

  for (KjNode* childP = objP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    ++count;
    if      (strcmp(childP->name, "type")              == 0)  typeP   = childP;
    else if (strcmp(childP->name, LD_VOCAB_COORDINATES) == 0)  coordsP = childP;
  }

  return (count == 2 && typeP != NULL && typeP->type == KjString && coordsP != NULL);
}



// -----------------------------------------------------------------------------
//
// isJsonLiteral - is this object a JSON literal { "@type":"@json", "@value":... } ?
//
// Per TS 104-175 clause 5, a JSON (null) literal {"@type":"@json","@value":X} is a
// permitted Attribute value — it is "not to be expanded in JSON-LD" and survives
// expansion. In simplified form the whole object IS the (Property) value: it is a
// single opaque value, NOT a fragment of sub-attributes and NOT a null to drop. So
// it must be wrapped as a Property value in BOTH create and merge modes — in merge
// mode that makes it REPLACE the target value, instead of its "@type"/"@value"
// members being merged in as sub-attributes (which corrupted the attribute — the
// json-literal-null merge splice).
//
static bool isJsonLiteral(KjNode* objP)
{
  for (KjNode* childP = objP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if ((strcmp(childP->name, "@type") == 0) && (childP->type == KjString) && (strcmp(childP->value.s, "@json") == 0))
      return true;
  }
  return false;
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
      // The attribute-type keywords above are core-context terms and are NEVER
      // JSON-LD-expanded — they always arrive in short form. A "type" value
      // that DID expand (to https://uri.etsi.org/ngsi-ld/default-context/<term>
      // via the core context's "@vocab") is by definition an unknown term, not
      // an NGSI-LD attribute type, so it must fall through. (An earlier broad
      // "starts with https://uri.etsi.org/ngsi-ld/" check wrongly accepted
      // exactly those @vocab expansions — e.g. a "Poinxt" geometry — and stored
      // them as junk instead of letting Case 3 detect + reject the geometry.)
    }
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// addTypeField - prepend a "type" string field to an attribute object
//
static void addTypeField(KjNode* attrP, const char* typeName, KAlloc* kaP)
{
  KjNode* typeNodeP = kjString(swRest.kjsonP, "type", typeName);
  if (typeNodeP != NULL)
  {
    // Structural member created here (after expansion) — stamp it so the
    // sub-attribute checks (KJF_ATTR_TERM) don't mistake it for a sub-attr.
    typeNodeP->flags |= KJF_CORE_TERM | KJF_ATTR_TERM;
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
static void wrapAsProperty(KjNode* entityP, KjNode* childP, KAlloc* kaP)
{
  KjNode* wrapperP = kjObject(swRest.kjsonP, childP->name);
  if (wrapperP == NULL)
    return;

  // Create a value node that copies the original's type+value
  KjNode* valueNodeP = (KjNode*) kaAlloc(kaP, sizeof(KjNode));
  if (valueNodeP == NULL)
    return;

  memset(valueNodeP, 0, sizeof(KjNode));
  valueNodeP->name  = (char*) LD_VOCAB_HAS_VALUE;
  valueNodeP->type  = childP->type;
  valueNodeP->value = childP->value;
  valueNodeP->next  = NULL;
  valueNodeP->flags = KJF_CORE_TERM | KJF_ATTR_TERM | (KJF_VK_VALUE << KJF_VK_SHIFT);

  kjChildAdd(wrapperP, valueNodeP);
  addTypeField(wrapperP, "Property", kaP);

  kjChildReplace(entityP, childP, wrapperP);
}



// -----------------------------------------------------------------------------
//
// ldWrapAsGeoProperty - wrap a GeoJSON object inside a GeoProperty
//
// Public for callers that already know the target field is a GeoProperty
// (typically CSR intake on location / observationSpace / operationSpace —
// bare GeoJSON on the wire per § 5.2.9, but stored and rendered as the
// normalized GeoProperty wrapper).
//
void ldWrapAsGeoProperty(KjNode* entityP, KjNode* childP, KAlloc* kaP)
{
  KjNode* wrapperP = kjObject(swRest.kjsonP, childP->name);
  if (wrapperP == NULL)
    return;

  // Create hasValue node pointing to the GeoJSON object's children
  KjNode* valueNodeP = (KjNode*) kaAlloc(kaP, sizeof(KjNode));
  if (valueNodeP == NULL)
    return;

  memset(valueNodeP, 0, sizeof(KjNode));
  valueNodeP->name  = (char*) LD_VOCAB_HAS_VALUE;
  valueNodeP->type  = childP->type;
  valueNodeP->value = childP->value;
  valueNodeP->next  = NULL;
  valueNodeP->flags = KJF_CORE_TERM | KJF_ATTR_TERM | (KJF_VK_VALUE << KJF_VK_SHIFT);

  kjChildAdd(wrapperP, valueNodeP);
  addTypeField(wrapperP, "GeoProperty", kaP);

  kjChildReplace(entityP, childP, wrapperP);
}



static void normalizeAttr(KjNode* containerP, KjNode* attrP, KAlloc* kaP, bool mergeMode);



// -----------------------------------------------------------------------------
//
// normalizeAttr - normalize a single attribute (may be object, scalar, or array)
//
// containerP: the parent node (entity or attribute object) — needed for kjChildReplace
// attrP:      the attribute node to normalize
//
static void normalizeAttr(KjNode* containerP, KjNode* attrP, KAlloc* kaP, bool mergeMode)
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

    wrapAsProperty(containerP, attrP, kaP);
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
          normalizeAttr(attrP, elemP, kaP, mergeMode);
        elemP = elemNextP;
      }
    }
    else
    {
      // Simplified array value (e.g. "tags": ["fast", "red"])
      wrapAsProperty(containerP, attrP, kaP);
    }
    return;
  }

  // ---  Object children  ---
  if (attrP->type != KjObject)
    return;

  // A JSON literal {"@type":"@json","@value":X} is a single opaque value (clause 5),
  // not a fragment of sub-attributes: wrap it as a Property value in BOTH create and
  // merge modes, so a merge replaces the target value instead of splicing @type/@value.
  if (isJsonLiteral(attrP))
  {
    wrapAsProperty(containerP, attrP, kaP);
    return;
  }

  // Case 1: Has explicit attr type (Property/Relationship/etc.) → already normalized or concise with type
  if (hasExplicitAttrType(attrP))
  {
    // Recurse into sub-attributes
    KjNode* subP = attrP->value.firstChildP;
    while (subP != NULL)
    {
      KjNode* subNextP = subP->next;
      if (isAttrKeyword(subP) == false)
        normalizeAttr(attrP, subP, kaP, mergeMode);
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
      addTypeField(attrP, "GeoProperty", kaP);
    }
    else
    {
      LdAttrType detectedType = ldAttrTypeDetect(attrP);
      if (detectedType != LdAttrNone)
        addTypeField(attrP, ldAttrTypeToString(detectedType), kaP);
    }

    // Recurse into sub-attributes
    KjNode* subP = attrP->value.firstChildP;
    while (subP != NULL)
    {
      KjNode* subNextP = subP->next;
      if (isAttrKeyword(subP) == false)
        normalizeAttr(attrP, subP, kaP, mergeMode);
      subP = subNextP;
    }
    return;
  }

  // Case 3: Object with no value key, no attr type
  // Check if it IS a GeoJSON geometry (simplified GeoProperty). A valid
  // geometry (isGeoJsonObject) OR the structural type+coordinates shape
  // (isSimplifiedGeoProperty) — the latter also catches an invalid geometry
  // type so ldCheckGeo can reject it instead of it being stored as junk.
  if (isGeoJsonObject(attrP) || isSimplifiedGeoProperty(attrP))
  {
    ldWrapAsGeoProperty(containerP, attrP, kaP);
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
      if (isAttrKeyword(subP) == false)
        normalizeAttr(attrP, subP, kaP, mergeMode);
      subP = subNextP;
    }
    return;
  }

  wrapAsProperty(containerP, attrP, kaP);
}



// -----------------------------------------------------------------------------
//
// ldNormalizeInput -
//
void ldNormalizeInput(KjNode* entityP, KAlloc* kaP, bool mergeMode)
{
  if (entityP == NULL || entityP->type != KjObject)
    return;

  KjNode* childP = entityP->value.firstChildP;

  while (childP != NULL)
  {
    KjNode* nextP = childP->next;  // save before normalizeAttr may replace childP

    if (ldIsEntityKeyword(childP->name) == false)
      normalizeAttr(entityP, childP, kaP, mergeMode);

    childP = nextP;
  }
}
