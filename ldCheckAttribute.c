//
// FILE            ldCheckAttribute.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strcmp
#include <ctype.h>                                       // isalpha, isalnum

#include "kbase/kLibLog.h"                             // KLOG_T
#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjLookup.h"                             // kjLookup
#include "corJsonld/corLdExpand.h"                        // corLdValueObjectIs, corLdValueObjectCheck

#include "corNgsild/LdAttrType.h"                         // LdAttrType
#include "corNgsild/LdOp.h"                               // LdOp
#include "corNgsild/LdCheck.h"                            // STRING_CHECK, URI_CHECK, ...
#include "corNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "corNgsild/ldTypes.h"                            // ldAttrTypeToString
#include "corNgsild/ldAttrTypeDetect.h"                   // ldAttrTypeDetect
#include "corNgsild/ldError.h"                            // ldError
#include "corNgsild/ldInit.h"                             // ldTypedValueCheck
#include "corNgsild/ldCheckGeo.h"                         // ldCheckGeo
#include "corNgsild/ldCheckAttribute.h"                   // Own interface
#include "corNgsild/ldTraceLevels.h"                      // LdTCheckAttr



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
// isSubAttrOnlyName - match core terms that are structural sub-attributes only
//
// `observedAt` (§ 5.2.4 TemporalProperty) and `unitCode` (§ 5.2.6) are defined
// by the core context exclusively as sub-attributes of an Attribute — they have
// no meaning as a top-level Attribute name. Core-context terms are not expanded,
// so they reach the validator in their short form.
//
static bool isSubAttrOnlyName(const char* name)
{
  if (name == NULL)
    return false;

  if (strcmp(name, LD_VOCAB_OBSERVED_AT) == 0)  return true;
  if (strcmp(name, LD_VOCAB_UNIT_CODE)   == 0)  return true;

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
// Core-context terms are never expanded, so they reach the validator in their
// short form — membership is detected by matching against those short names.
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
  if (strcmp(name, "objectType")              == 0)  return true;

  return false;
}



// -----------------------------------------------------------------------------
//
// checkPartialSubAttrs - validate structural sub-attributes on a type-less fragment
//
// A partial update (Merge Entity / Partial Attribute Update) may carry a fragment
// with no detectable attribute type — e.g. `{"observedAt": "..."}` that touches
// only a structural sub-attribute of an existing instance. The main value/sub-
// field validation (Step 5) is skipped for such a fragment because no attribute
// type is known, so the structural core terms that have a FIXED shape regardless
// of the attribute type must still be validated here. Otherwise an invalid value
// (e.g. a non-DateTime observedAt) grafts unchecked onto the stored instance and
// is silently coerced (a bad observedAt became 1970-01-01T00:00:00Z).
//   § 5.2.4 observedAt is a DateTime · § 5.2.6 unitCode is a string ·
//   datasetId is a URI string.
//
static bool checkPartialSubAttrs(KjNode* attrP, bool nullAllowed)
{
  KjNode* observedAtP = kjLookup(attrP, LD_VOCAB_OBSERVED_AT);
  KjNode* unitCodeP   = kjLookup(attrP, LD_VOCAB_UNIT_CODE);
  KjNode* datasetIdP  = kjLookup(attrP, LD_VOCAB_DATASET_ID);

  if (observedAtP != NULL)
  {
    if (observedAtP->type != KjString)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid observedAt", "Attribute '%s': 'observedAt' must be a string", attrP->name);
      return false;
    }
    // § 4.5.5.9 — in a merge/update fragment the NGSI-LD Null marker on observedAt
    // removes the sub-attribute; accept it without the DateTime shape check (the
    // removal is applied downstream, as for any other sub-attribute).
    if (!(nullAllowed && strcmp(observedAtP->value.s, LD_VOCAB_NGSILD_NULL) == 0))
    {
      if (!ldCheckDateTime(observedAtP->value.s, NULL))
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid observedAt", "Attribute '%s': 'observedAt' is not a valid ISO 8601 DateTime: '%s'", attrP->name, observedAtP->value.s);
        return false;
      }
    }
  }

  if ((unitCodeP != NULL) && (unitCodeP->type != KjString))
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid unitCode", "Attribute '%s': 'unitCode' must be a string", attrP->name);
    return false;
  }

  if (datasetIdP != NULL)
  {
    if (datasetIdP->type != KjString)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid datasetId", "Attribute '%s': 'datasetId' must be a URI string", attrP->name);
      return false;
    }
    if (strcmp(datasetIdP->value.s, LD_VOCAB_NGSILD_NULL) == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid datasetId",
              "Attribute '%s': a 'datasetId' cannot be set to the NGSI-LD Null 'urn:ngsi-ld:null' — it identifies an Attribute instance and cannot be deleted this way (§ 8.4.2)", attrP->name);
      return false;
    }
    URI_CHECK(datasetIdP->value.s);
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// isAllowedCoreAttrTerm - check if a core context term is allowed in an attribute
//
static bool isAllowedCoreAttrTerm(const char* name, const char* valueKey, LdAttrType attrType)
{
  if (strcmp(name, "type")              == 0)  return true;
  if (strcmp(name, valueKey)            == 0)  return true;
  if (strcmp(name, LD_VOCAB_OBSERVED_AT) == 0)  return true;
  if (strcmp(name, LD_VOCAB_DATASET_ID) == 0)  return true;

  // valueType qualifies a value for the whole Property family (§ 5.2.6) — every
  // attribute type except a Relationship / ListRelationship (object/objectList).
  if (strcmp(name, "valueType") == 0)
    return ((strcmp(valueKey, LD_VOCAB_HAS_OBJECT) != 0) && (strcmp(valueKey, LD_VOCAB_HAS_OBJECT_LIST) != 0));

  // unitCode is a unit for a numeric/plain value — only Property and ListProperty
  // (§ 5.2.6.4.7/10/5). GeoProperty shares the "value" key with Property, so the
  // attribute type (not the value key) has to be checked to exclude it, along
  // with LanguageProperty / VocabProperty / JsonProperty which are unitless too.
  if (strcmp(name, LD_VOCAB_UNIT_CODE) == 0)
    return ((attrType == LdAttrProperty) || (attrType == LdAttrListProperty));

  // objectType qualifies a relationship's target — only for object / objectList.
  if (strcmp(name, "objectType") == 0)
    return ((strcmp(valueKey, LD_VOCAB_HAS_OBJECT) == 0) || (strcmp(valueKey, LD_VOCAB_HAS_OBJECT_LIST) == 0));

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
// languageTagWellFormed - is `tag` a well-formed IETF RFC 5646 language tag?
//
// § 5.2.6.4.6: a languageMap key "shall be a JSON string representing an IETF RFC
// 5646 language code, or the JSON-LD @none". This checks SHAPE, not registration -
// "zz" and "qqq-ZZ" are well-formed and accepted even though no such language is
// registered. Validating against the IANA registry would need the registry itself,
// and would reject tags that become valid when it is next updated.
//
//   primary subtag  2-8 ALPHA          en, deu, klingon
//   further subtags 1-8 ALPHANUM each  en-US, zh-Hans-CN, de-CH-1901
//   singletons      x-... / i-...      private-use and grandfathered forms
//
static bool languageTagWellFormed(const char* tag)
{
  int i = 0;

  // Private-use (x-) and grandfathered (i-) tags start with a one-letter singleton.
  if (((tag[0] == 'x') || (tag[0] == 'X') || (tag[0] == 'i') || (tag[0] == 'I')) && (tag[1] == '-'))
    i = 1;
  else
  {
    while (isalpha((unsigned char) tag[i]))
      i++;
    if ((i < 2) || (i > 8))                                 // primary subtag length
      return false;
    if (tag[i] == 0)
      return true;
  }

  // Remaining subtags: '-' followed by 1-8 alphanumerics, repeated.
  while (tag[i] == '-')
  {
    int len = 0;
    i++;
    while (isalnum((unsigned char) tag[i]))
    {
      i++;
      len++;
    }
    if ((len < 1) || (len > 8))
      return false;
  }

  return (tag[i] == 0);
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
    // § 5.2.6.4.6 — a languageMap key shall be an RFC 5646 language tag, or @none
    // (the JSON-LD default when no more specific language matches; it is also what
    // the NGSI-LD Null encoding {"@none": "urn:ngsi-ld:null"} uses).
    if (childP->name == NULL || childP->name[0] == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid LanguageProperty", "'languageMap' has an empty language key");
      return false;
    }

    if ((strcmp(childP->name, "@none") != 0) && (!languageTagWellFormed(childP->name)))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid LanguageProperty",
              "'languageMap' key is not an RFC 5646 language tag: '%s'", childP->name);
      return false;
    }

    if (childP->type != KjString && childP->type != KjArray)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid LanguageProperty", "languageMap values must be strings or arrays of strings (key '%s')", childP->name);
      return false;
    }

    // A scalar value must be a non-empty string.
    if (childP->type == KjString && childP->value.s[0] == 0)
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid LanguageProperty", "languageMap value for '%s' is an empty string", childP->name);
      return false;
    }

    // An array value must be non-empty and hold only non-empty strings.
    if (childP->type == KjArray)
    {
      if (childP->value.firstChildP == NULL)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid LanguageProperty", "languageMap value for '%s' is an empty array", childP->name);
        return false;
      }
      for (KjNode* elemP = childP->value.firstChildP; elemP != NULL; elemP = elemP->next)
      {
        if (elemP->type != KjString || elemP->value.s[0] == 0)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid LanguageProperty", "languageMap value for '%s' must be a non-empty string", childP->name);
          return false;
        }
      }
    }

    // § 5.2.6.4.6 — an array of ONE string collapses to a scalar on storage,
    // so it round-trips as a String in every format and under lang reduction.
    if ((childP->type == KjArray) &&
        (childP->value.firstChildP != NULL) &&
        (childP->value.firstChildP->next == NULL) &&
        (childP->value.firstChildP->type == KjString))
    {
      KjNode* onlyP = childP->value.firstChildP;
      childP->type    = KjString;
      childP->value.s = onlyP->value.s;
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

  // `observedAt` / `unitCode` are core structural sub-attributes (§ 5.2.4 /
  // § 5.2.6) — they have no meaning as a top-level Attribute name.
  if (isSubAttrOnlyName(attrP->name))
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Reserved Attribute Name",
            "'%s' is a structural sub-attribute and cannot be used as an Attribute name",
            attrP->name);
    return false;
  }

  // Step 2: Check for attribute type change
  if (attrTypeFromDb != LdAttrNone && attrType != LdAttrNone && attrType != attrTypeFromDb)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Attribute Type Change", "Cannot change attribute '%s' type from %s to %s", attrP->name, ldAttrTypeToString(attrTypeFromDb), ldAttrTypeToString(attrType));
    return false;
  }

  // urn:ngsi-ld:null on an attribute value or sub-attribute is the delete marker,
  // permitted in the merge/update flows (§ 4.5.5.9). Computed here so the type-less
  // partial-fragment path honours it too (a bare {observedAt: "urn:ngsi-ld:null"}
  // removes observedAt), and reused for the value-marker check further down.
  const bool nullAllowed = (op == LdOpMergeEntity) || (op == LdOpBatchMerge) ||
                           (op == LdOpUpdateEntity) || (op == LdOpUpdateAttrs);

  // If no type detected (might be a partial update), the type-specific value
  // checks below don't apply — but a structural sub-attribute carried by the
  // fragment (observedAt / unitCode / datasetId) still has a fixed shape and
  // must be validated, or an invalid value grafts unchecked onto the stored
  // instance.
  if (attrType == LdAttrNone)
    return checkPartialSubAttrs(attrP, nullAllowed);

  // Step 3: Check required value field and validate it
  const char*  expectedKey    = valueKeyForType(attrType);
  KjNode*      valueNodeP     = NULL;
  KjNode*      wrongValueKeyP = NULL;
  int          valueKeyCount  = 0;

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
      else if (wrongValueKeyP == NULL)
        wrongValueKeyP = childP;  // a value-key of the wrong kind for this type (e.g. "value" on a Relationship)
    }
  }

  if (valueKeyCount > 1)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Conflicting Value Keys", "Attribute '%s' has %d value keys (only one allowed)", attrP->name, valueKeyCount);
    return false;
  }

  if (valueNodeP == NULL)
  {
    if (wrongValueKeyP != NULL)
      // The user gave a value-key that belongs to a different attribute type — e.g. "value" on a
      // Relationship, whose value (its target) goes in "object". Name both so the fix is obvious.
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Wrong Value Field",
              "Attribute '%s' of type %s carries its value in '%s', not '%s'",
              attrP->name, ldAttrTypeToString(attrType), expectedKey, wrongValueKeyP->name);
    else
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Missing Value",
              "Attribute '%s' of type %s must have '%s'",
              attrP->name, ldAttrTypeToString(attrType), expectedKey);
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
  // nullAllowed is computed above (before the type-less partial-fragment path).

  // A scalar value/object/vocab carrying the sentinel. A JsonProperty's `json`
  // is opaque (value-opaqueness) — there the sentinel is literal data, never a
  // marker — so it is deliberately excluded.
  if (valueNodeP->type == KjString && strcmp(valueNodeP->value.s, "urn:ngsi-ld:null") == 0)
  {
    if (!nullAllowed && (attrType == LdAttrProperty || attrType == LdAttrRelationship || attrType == LdAttrVocabProperty))
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value", "'urn:ngsi-ld:null' is not allowed as the value of attribute '%s'", attrP->name);
      return false;
    }
    if (nullAllowed)
      return true;  // skip type-specific shape checks for the marker
  }
  // A LanguageProperty carries the sentinel INSIDE the map, as
  // {"@none": "urn:ngsi-ld:null"} (§ 5.2.6.4.6), so the scalar test above never
  // sees it — the value node is an object. § 8.2.3 puts it under the same rule:
  // using that form as the right-hand side of languageMap is BadRequestData
  // except in the fragments of update/merge, and in notifications and the
  // temporal evolution. Without this a Create stored the delete marker as if it
  // were data: {"languageMap": {"@none": "urn:ngsi-ld:null"}} came back from a
  // subsequent GET verbatim.
  else if ((attrType == LdAttrLanguageProperty) && (valueNodeP->type == KjObject))
  {
    KjNode* noneP = kjLookup(valueNodeP, "@none");

    if ((noneP != NULL) && (noneP->type == KjString) && (strcmp(noneP->value.s, "urn:ngsi-ld:null") == 0))
    {
      if (!nullAllowed)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value",
                "'urn:ngsi-ld:null' is not allowed as the languageMap of attribute '%s'", attrP->name);
        return false;
      }

      return true;  // the delete marker — skip the type-specific shape checks
    }
  }
  // Inside a List value the sentinel can only be erroneous input: a single list
  // element is not a delete position (the merge/update flows delete a whole
  // attribute via a top-level sentinel). § 4.5.5: an NGSI-LD Null encountered
  // anywhere it has no delete meaning is BadRequestData.
  else if ((attrType == LdAttrListProperty || attrType == LdAttrListRelationship) && valueNodeP->type == KjArray)
  {
    for (KjNode* elemP = valueNodeP->value.firstChildP; elemP != NULL; elemP = elemP->next)
    {
      if (elemP->type == KjString && strcmp(elemP->value.s, "urn:ngsi-ld:null") == 0)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value", "'urn:ngsi-ld:null' is not allowed as a list element of attribute '%s'", attrP->name);
        return false;
      }
    }
  }

  // Step 4: Type-specific value validation
  switch (attrType)
  {
  case LdAttrRelationship:
    // 'object' is a URI string or, per spec clause 5 (String or String[]), an array of URI strings.
    if (valueNodeP->type == KjString)
      URI_CHECK(valueNodeP->value.s);
    else if (valueNodeP->type == KjArray)
    {
      for (KjNode* itemP = valueNodeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
      {
        if (itemP->type != KjString)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Relationship", "Relationship '%s': 'object' array items must be URI strings", attrP->name);
          return false;
        }
        URI_CHECK(itemP->value.s);
      }
    }
    else
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Relationship", "Relationship '%s': 'object' must be a URI string or an array of URI strings", attrP->name);
      return false;
    }
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

    // Array of one - collapse to scalar so it round-trips as a String.
    if ((valueNodeP->type == KjArray) &&
        (valueNodeP->value.firstChildP != NULL) &&
        (valueNodeP->value.firstChildP->next == NULL) &&
        (valueNodeP->value.firstChildP->type == KjString))
    {
      KjNode* onlyP       = valueNodeP->value.firstChildP;
      valueNodeP->type    = KjString;
      valueNodeP->value.s = onlyP->value.s;
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

  case LdAttrProperty:
    // A Property value may be a JSON-LD typed value { "@type":…, "@value":… }.
    // corJsonld owns its structural rules; NGSI-LD owns the datatype semantics
    // (e.g. @type:DateTime ⇒ @value must be a valid ISO 8601 DateTime).
    if ((valueNodeP->type == KjObject) && corLdValueObjectIs(valueNodeP))
    {
      char* detail = NULL;
      if (corLdValueObjectCheck(valueNodeP, &detail) == false)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid Value Object", "Attribute '%s': %s", attrP->name, detail);
        return false;
      }

      // Validate @value against its @type with the SHARED typed-value checker
      // (DateTime + every xsd datatype), so a value-object is validated the same
      // here as in the JSON-LD expansion / free-property path — not DateTime
      // only. NGSI-LD `DateTime` maps onto xsd:dateTime (the checker keys off
      // xsd local names); attrContext=true phrases the error as an attribute one.
      KjNode* atTypeP  = kjLookup(valueNodeP, "@type");
      KjNode* atValueP = kjLookup(valueNodeP, "@value");
      if ((atTypeP != NULL) && (atTypeP->type == KjString) && (atValueP != NULL))
      {
        const char* dt = atTypeP->value.s;
        if ((strcmp(dt, "DateTime") == 0) || (strcmp(dt, "https://uri.etsi.org/ngsi-ld/DateTime") == 0))
          dt = "xsd:dateTime";
        if (ldTypedValueCheck(attrP->name, dt, atValueP, /*attrContext*/true) == false)
          return false;
      }
    }
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
      if (isAllowedCoreAttrTerm(childP->name, expectedKey, attrType) == false)
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
        // § 5.4.1: in a merge/update fragment the NGSI-LD Null marker on
        // observedAt removes the sub-attribute — accept it without running the
        // DateTime shape check (the removal is applied downstream).
        if (!(nullAllowed && strcmp(childP->value.s, LD_VOCAB_NGSILD_NULL) == 0))
        {
          if (!ldCheckDateTime(childP->value.s, NULL))
          {
            ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid observedAt", "Attribute '%s': 'observedAt' is not a valid ISO 8601 DateTime: '%s'", attrP->name, childP->value.s);
            return false;
          }
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
        if (strcmp(childP->value.s, LD_VOCAB_NGSILD_NULL) == 0)
        {
          ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid datasetId",
                  "Attribute '%s': a 'datasetId' cannot be set to the NGSI-LD Null 'urn:ngsi-ld:null' — it identifies an Attribute instance and cannot be deleted this way (§ 8.4.2)", attrP->name);
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
