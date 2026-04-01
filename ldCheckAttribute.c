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
