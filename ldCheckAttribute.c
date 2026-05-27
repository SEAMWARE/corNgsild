//
// FILE            ldCheckAttribute.c
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

#include "swNgsild/LdAttrType.h"                         // LdAttrType
#include "swNgsild/LdOp.h"                               // LdOp
#include "swNgsild/LdCheck.h"                            // STRING_CHECK, URI_CHECK, ...
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/ldTypes.h"                            // ldAttrTypeToString
#include "swNgsild/ldAttrTypeDetect.h"                   // ldAttrTypeDetect
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldCheckGeo.h"                         // ldCheckGeo
#include "swNgsild/ldCheckAttribute.h"                   // Own interface
#include "swNgsild/ldTraceLevels.h"                      // LdTCheckAttr



// -----------------------------------------------------------------------------
//
// isWellKnownGeoName - match the three names spec § 4.7 reserves as GeoProperty
//
// Per § 4.7, `location`, `observationSpace`, `operationSpace` are well-known
// GeoProperty attribute names — entities MUST NOT use them for any other
// attribute type. The validator runs after parseHook expansion so we accept
// both the short form (raw application/json bodies) and the @vocab-expanded
// IRI form.
//
static bool isWellKnownGeoName(const char* name)
{
  if (name == NULL)
    return false;

  if (strcmp(name, "location")         == 0)  return true;
  if (strcmp(name, "observationSpace") == 0)  return true;
  if (strcmp(name, "operationSpace")   == 0)  return true;

  if (strcmp(name, "https://uri.etsi.org/ngsi-ld/location")         == 0)  return true;
  if (strcmp(name, "https://uri.etsi.org/ngsi-ld/observationSpace") == 0)  return true;
  if (strcmp(name, "https://uri.etsi.org/ngsi-ld/operationSpace")   == 0)  return true;

  return false;
}



// -----------------------------------------------------------------------------
//
// valueKeyForType - return the expected value key for an attribute type
//
static const char* valueKeyForType(LdAttrType attrType)
{
  switch (attrType)
  {
  case LdAttrProperty:         return LD_VOCAB_HAS_VALUE;
  case LdAttrRelationship:     return LD_VOCAB_HAS_OBJECT;
  case LdAttrGeoProperty:      return LD_VOCAB_HAS_VALUE;
  case LdAttrLanguageProperty: return LD_VOCAB_HAS_LANGUAGE_MAP;
  case LdAttrVocabProperty:    return LD_VOCAB_HAS_VOCAB;
  case LdAttrListProperty:     return LD_VOCAB_HAS_VALUE_LIST;
  case LdAttrListRelationship: return LD_VOCAB_HAS_OBJECT_LIST;
  case LdAttrJsonProperty:     return LD_VOCAB_HAS_JSON;
  default:                     return NULL;
  }
}



// -----------------------------------------------------------------------------
//
// ldIsCoreAttrTerm - check if a name is a known NGSI-LD core context attribute-level term
//
// Replaces the FT_AUX_CORE_CONTEXT flag check from fwNgsild. Since KjNode has no aux
// field, we detect core context membership by matching against known expanded IRIs.
//
static bool ldIsCoreAttrTerm(const char* name)
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
  if (strcmp(name, "valueType")               == 0)  return true;

  return false;
}



// -----------------------------------------------------------------------------
//
// isAllowedCoreAttrTerm - check if a core context term is allowed in an attribute
//
static bool isAllowedCoreAttrTerm(const char* name, const char* valueKey)
{
  if (strcmp(name, "type")              == 0)  return true;
  if (strcmp(name, valueKey)            == 0)  return true;
  if (strcmp(name, LD_VOCAB_OBSERVED_AT) == 0)  return true;
  if (strcmp(name, LD_VOCAB_UNIT_CODE)  == 0)  return true;
  if (strcmp(name, LD_VOCAB_DATASET_ID) == 0)  return true;

  // valueType is valid for the Property family only (§ 5.2.x) — not for a
  // Relationship / ListRelationship (object / objectList).
  if (strcmp(name, "valueType") == 0)
    return ((strcmp(valueKey, LD_VOCAB_HAS_OBJECT) != 0) && (strcmp(valueKey, LD_VOCAB_HAS_OBJECT_LIST) != 0));

  return false;
}



// -----------------------------------------------------------------------------
//
// isValueKey - check if a name is one of the NGSI-LD value keys
//
static bool isValueKey(const char* name)
{
  if (strcmp(name, LD_VOCAB_HAS_VALUE)        == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_OBJECT)       == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_LANGUAGE_MAP) == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_VOCAB)        == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_VALUE_LIST)   == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_OBJECT_LIST)  == 0)  return true;
  if (strcmp(name, LD_VOCAB_HAS_JSON)         == 0)  return true;

  return false;
}



// -----------------------------------------------------------------------------
//
// checkLanguageMap - validate a languageMap value (object with string values)
//
static bool checkLanguageMap(KjNode* lmP)
{
  if (lmP->type != KjObject)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid LanguageProperty", "'languageMap' must be a JSON object");
    return false;
  }

  for (KjNode* childP = lmP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (childP->type != KjString && childP->type != KjArray)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid LanguageProperty", "languageMap values must be strings or arrays of strings (key '%s')", childP->name);
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// checkObjectList - validate an objectList (array of URIs)
//
static bool checkObjectList(KjNode* listP)
{
  if (listP->type != KjArray)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid ListRelationship", "'objectList' must be a JSON array");
    return false;
  }

  for (KjNode* itemP = listP->value.firstChildP; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->type != KjString)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid ListRelationship", "'objectList' items must be URI strings");
      return false;
    }
    URI_CHECK(itemP->value.s);
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// ldCheckAttribute -
//
bool ldCheckAttribute(KjNode* attrP, LdOp op, LdAttrType attrTypeFromDb, KAlloc* faP)
{
  if (attrP == NULL)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Attribute", "Attribute is NULL");
    return false;
  }

  // If it's not an object, it's simplified format - nothing more to validate
  if (attrP->type != KjObject)
    return true;

  // Step 1: Detect attribute type
  LdAttrType attrType = ldAttrTypeDetect(attrP);

  KLOG_T(LdTCheckAttr, "Checking attribute '%s', detected type: %s", attrP->name, ldAttrTypeToString(attrType));

  // § 4.7 — `location`, `observationSpace`, `operationSpace` are well-known
  // GeoProperty names; an attribute carrying one of these names cannot be
  // declared as anything else. Skip when type is unknown (partial update).
  if (attrType != LdAttrNone && attrType != LdAttrGeoProperty && isWellKnownGeoName(attrP->name))
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Attribute",
            "Attribute '%s' is reserved by spec § 4.7 and must be of type GeoProperty (got %s)",
            attrP->name, ldAttrTypeToString(attrType));
    return false;
  }

  // Step 2: Check for attribute type change
  if (attrTypeFromDb != LdAttrNone && attrType != LdAttrNone && attrType != attrTypeFromDb)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Attribute Type Change", "Cannot change attribute '%s' type from %s to %s", attrP->name, ldAttrTypeToString(attrTypeFromDb), ldAttrTypeToString(attrType));
    return false;
  }

  // If no type detected (might be a partial update), skip value checks
  if (attrType == LdAttrNone)
    return true;

  // Step 3: Check required value field and validate it
  const char*  expectedKey  = valueKeyForType(attrType);
  KjNode*      valueNodeP   = NULL;
  int          valueKeyCount = 0;

  for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    // JSON null not allowed inside attributes (except JsonProperty's "json" value is opaque)
    if (childP->type == KjNull && attrType != LdAttrJsonProperty)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value", "JSON null is not allowed in NGSI-LD (attribute '%s', field '%s')", attrP->name, childP->name);
      return false;
    }

    if (isValueKey(childP->name) == true)
    {
      ++valueKeyCount;
      if (strcmp(childP->name, expectedKey) == 0)
        valueNodeP = childP;
    }
  }

  if (valueKeyCount > 1)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Conflicting Value Keys", "Attribute '%s' has %d value keys (only one allowed)", attrP->name, valueKeyCount);
    return false;
  }

  if (valueNodeP == NULL)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Missing Value", "Attribute '%s' of type %s must have '%s'", attrP->name, ldAttrTypeToString(attrType), expectedKey);
    return false;
  }

  // urn:ngsi-ld:null on an attribute value is the delete marker (§ 4.5.5.9,
  // § 5.6.6, § 5.6.2.4). Permitted for Merge Entity, Batch Merge, and
  // Update Entity / Update Attributes (the spec allows instance-level
  // deletion via null in those flows). Rejected for other ops (Create,
  // Append, Replace) where null markers have no defined meaning.
  //
  // When the marker is permitted, short-circuit the rest of the value
  // validation — the type-specific checks below (ldCheckGeo for
  // GeoProperty, checkLanguageMap, etc.) would reject the bare string
  // as the wrong shape (ETSI 011_07_03 / 012_05_03 / 056_03_03 etc.).
  if (valueNodeP->type == KjString && strcmp(valueNodeP->value.s, "urn:ngsi-ld:null") == 0)
  {
    bool nullAllowed = (op == LdOpMergeEntity) || (op == LdOpBatchMerge) ||
                       (op == LdOpUpdateEntity) || (op == LdOpUpdateAttrs);
    if (!nullAllowed && (attrType == LdAttrProperty || attrType == LdAttrRelationship))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value", "'urn:ngsi-ld:null' is not allowed as the value/object of attribute '%s'", attrP->name);
      return false;
    }
    if (nullAllowed)
      return true;  // skip type-specific shape checks for the marker
  }

  // Step 4: Type-specific value validation
  switch (attrType)
  {
  case LdAttrRelationship:
    if (valueNodeP->type != KjString)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Relationship", "Relationship '%s': 'object' must be a URI string", attrP->name);
      return false;
    }
    URI_CHECK(valueNodeP->value.s);
    break;

  case LdAttrGeoProperty:
    if (ldCheckGeo(valueNodeP) == false)
      return false;
    break;

  case LdAttrLanguageProperty:
    if (checkLanguageMap(valueNodeP) == false)
      return false;
    break;

  case LdAttrVocabProperty:
    if (valueNodeP->type != KjString && valueNodeP->type != KjArray)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid VocabProperty", "VocabProperty '%s': 'vocab' must be a string or array of strings", attrP->name);
      return false;
    }
    break;

  case LdAttrListProperty:
    if (valueNodeP->type != KjArray)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid ListProperty", "ListProperty '%s': 'valueList' must be an array", attrP->name);
      return false;
    }
    break;

  case LdAttrListRelationship:
    if (checkObjectList(valueNodeP) == false)
      return false;
    break;

  default:
    break;
  }

  // Step 5: Check sub-fields (optional fields + forbidden core terms + sub-attributes)
  for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    // Core context term -- must be in the allowlist for this attribute type
    if (ldIsCoreAttrTerm(childP->name))
    {
      if (isAllowedCoreAttrTerm(childP->name, expectedKey) == false)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Attribute Member", "Attribute '%s' of type %s has a forbidden NGSI-LD term: '%s'", attrP->name, ldAttrTypeToString(attrType), childP->name);
        return false;
      }

      if (strcmp(childP->name, LD_VOCAB_OBSERVED_AT) == 0)
      {
        if (childP->type != KjString)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid observedAt", "Attribute '%s': 'observedAt' must be a string", attrP->name);
          return false;
        }
        if (ldCheckDateTime(childP->value.s) < 0)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid observedAt", "Attribute '%s': 'observedAt' is not a valid ISO 8601 DateTime: '%s'", attrP->name, childP->value.s);
          return false;
        }
      }
      else if (strcmp(childP->name, LD_VOCAB_UNIT_CODE) == 0)
      {
        if (childP->type != KjString)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid unitCode", "Attribute '%s': 'unitCode' must be a string", attrP->name);
          return false;
        }
      }
      else if (strcmp(childP->name, LD_VOCAB_DATASET_ID) == 0)
      {
        if (childP->type != KjString)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid datasetId", "Attribute '%s': 'datasetId' must be a URI string", attrP->name);
          return false;
        }
        URI_CHECK(childP->value.s);
      }
      else if (strcmp(childP->name, "valueType") == 0)
      {
        if (childP->type != KjString)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid valueType", "Attribute '%s': 'valueType' must be a string", attrP->name);
          return false;
        }
      }

      continue;  // Core term handled -- skip sub-attribute recursion
    }

    // Not a core context term -- it's a user-defined sub-attribute
    if (childP->type == KjObject)
    {
      if (ldCheckAttribute(childP, op, LdAttrNone, faP) == false)
        return false;
    }
  }

  KLOG_T(LdTCheckAttr, "Attribute '%s' valid (type: %s)", attrP->name, ldAttrTypeToString(attrType));
  return true;
}
