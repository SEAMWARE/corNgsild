//
// FILE            ldRender.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include <stdbool.h>                                     // bool
#include "corRest/corRest.h"                            // corRest
#include <string.h>                                      // strcmp

#include "kbase/kLibLog.h"                             // KLOG_T
#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjBuilder.h"                             // kjString
#include "kjson/kjLookup.h"                              // kjLookup

#include "corNgsild/LdAttrType.h"                         // LdAttrType
#include "corNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "corNgsild/ldTypes.h"                            // ldAttrTypeToString
#include "corNgsild/ldAttrTypeDetect.h"                   // ldAttrTypeDetect
#include "corNgsild/ldIsEntityKeyword.h"                   // ldIsEntityKeyword
#include "corNgsild/ldRender.h"                           // Own interface
#include "corNgsild/ldTraceLevels.h"                      // LdTRender



// -----------------------------------------------------------------------------
//
// isAttrKeyword -
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
// attrTypeCanBeInferred - check if the type can be inferred from the value key
//
// Property ("value") and GeoProperty ("value") share the same value key,
// so GeoProperty cannot be inferred from key alone.
//
static bool attrTypeCanBeInferred(LdAttrType attrType)
{
  switch (attrType)
  {
  case LdAttrProperty:         return true;    // from hasValue
  case LdAttrRelationship:     return true;    // from hasObject
  case LdAttrLanguageProperty: return true;    // from hasLanguageMap
  case LdAttrVocabProperty:    return true;    // from hasVocab
  case LdAttrListProperty:     return true;    // from hasValueList
  case LdAttrListRelationship: return true;    // from hasObjectList
  case LdAttrJsonProperty:     return true;    // from hasJSON
  case LdAttrGeoProperty:      return false;   // shares hasValue with Property
  default:                     return false;
  }
}



// =============================================================================
//
// toNormalized - ensure every attribute has an explicit "type" field
//
// =============================================================================



// -----------------------------------------------------------------------------
//
// attrToNormalized -
//
static void attrToNormalized(KjNode* attrP, KAlloc* faP)
{
  if (attrP->type != KjObject)
    return;

  LdAttrType attrType = ldAttrTypeDetect(attrP);
  if (attrType == LdAttrNone)
    return;

  // Check if "type" already exists
  bool hasType = false;
  for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, "type") == 0)
    {
      hasType = true;
      break;
    }
  }

  // Add "type" if missing
  if (hasType == false)
  {
    KjNode* typeNodeP = kjString(corRest.kjsonP, "type", ldAttrTypeToString(attrType));
    if (typeNodeP != NULL)
    {
      // Insert at the beginning
      typeNodeP->next = attrP->value.firstChildP;
      attrP->value.firstChildP = typeNodeP;
    }
  }

  // Recurse into sub-attributes
  for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (isAttrKeyword(childP->name) == false)
      attrToNormalized(childP, faP);
  }
}



// -----------------------------------------------------------------------------
//
// ldToNormalized -
//
bool ldToNormalized(KjNode* entityP, KAlloc* faP)
{
  if (entityP == NULL || entityP->type != KjObject)
    return false;

  for (KjNode* childP = entityP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (ldIsEntityKeyword(childP->name) == false)
      attrToNormalized(childP, faP);
  }

  KLOG_T(LdTRender, "Entity converted to normalized format");
  return true;
}



// =============================================================================
//
// toConcise - remove "type" when it can be inferred from the value key
//
// =============================================================================



// -----------------------------------------------------------------------------
//
// attrToConcise -
//
static void attrToConcise(KjNode* attrP)
{
  if (attrP->type != KjObject)
    return;

  LdAttrType attrType = ldAttrTypeDetect(attrP);
  if (attrType == LdAttrNone)
    return;

  // Remove "type" if it can be inferred
  if (attrTypeCanBeInferred(attrType) == true)
  {
    KjNode* prevP = NULL;
    for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
    {
      if (strcmp(childP->name, "type") == 0)
      {
        if (prevP == NULL)
          attrP->value.firstChildP = childP->next;
        else
          prevP->next = childP->next;

        if (attrP->lastChild == childP)
          attrP->lastChild = prevP;

        break;
      }
      prevP = childP;
    }
  }

  // Recurse into sub-attributes
  for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (isAttrKeyword(childP->name) == false)
      attrToConcise(childP);
  }

  // A Property or GeoProperty with no sub-attributes (sysAttrs are scalars
  // dropped earlier) carries ONLY its value, so concise == simplified: emit the
  // bare value — scalar, array, or (for GeoProperty) the GeoJSON object. Only
  // these two types may collapse; every other type keeps its value-key so the
  // concise form stays lossless (a bare array/object would be indistinguishable
  // from a Property value). GeoProperty's "type" is not inferable from the
  // "value" key (Property uses it too), so it is still present here — ignore it
  // when checking value-only, then discard it by replacing the attr with value.
  if ((attrType == LdAttrProperty) || (attrType == LdAttrGeoProperty))
  {
    KjNode*  valueP    = NULL;
    bool     valueOnly = true;

    for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
    {
      if (strcmp(childP->name, "type") == 0)
        continue;
      else if (strcmp(childP->name, LD_VOCAB_HAS_VALUE) == 0)
        valueP = childP;
      else
        valueOnly = false;
    }

    if ((valueOnly == true) && (valueP != NULL))
    {
      attrP->type  = valueP->type;
      attrP->value = valueP->value;
    }
  }
}



// -----------------------------------------------------------------------------
//
// ldToConcise -
//
bool ldToConcise(KjNode* entityP, KAlloc* faP)
{
  if (entityP == NULL || entityP->type != KjObject)
    return false;

  for (KjNode* childP = entityP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (ldIsEntityKeyword(childP->name) == true)
      continue;

    //
    // § 5.3.2.3 Normalized-to-Concise step 1: "In the multi-attribute case (see
    // clause 8.5), this becomes a series of values (an array of JSON objects)
    // and the operation shall be run on each element of the array."
    //
    // So a multi-attribute compacts per instance, exactly like a single one -
    // datasetId is a defined member of the Attribute type (step 4) and stays.
    // Skipping the array is what left every instance carrying its "type" while
    // the single-instance attribute beside it had dropped it.
    //
    if (childP->type == KjArray)
    {
      for (KjNode* instP = childP->value.firstChildP; instP != NULL; instP = instP->next)
        attrToConcise(instP);
    }
    else
      attrToConcise(childP);
  }

  KLOG_T(LdTRender, "Entity converted to concise format");
  return true;
}



// =============================================================================
//
// toSimplified - flatten attributes to plain key-value pairs (lossy)
//
// =============================================================================



// -----------------------------------------------------------------------------
//
// ldAttrValueNode - find the value-holding node of a normalized/concise attribute
//
// Returns the member that carries the attribute's value, whichever it is for the
// attribute type: value / object / languageMap / vocab / valueList / objectList /
// json. NULL if none present.
//
KjNode* ldAttrValueNode(KjNode* attrP)
{
  if (attrP->type != KjObject)
    return NULL;

  for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, LD_VOCAB_HAS_VALUE)        == 0)  return childP;
    if (strcmp(childP->name, LD_VOCAB_HAS_OBJECT)       == 0)  return childP;
    if (strcmp(childP->name, LD_VOCAB_HAS_LANGUAGE_MAP) == 0)  return childP;
    if (strcmp(childP->name, LD_VOCAB_HAS_VOCAB)        == 0)  return childP;
    if (strcmp(childP->name, LD_VOCAB_HAS_VALUE_LIST)   == 0)  return childP;
    if (strcmp(childP->name, LD_VOCAB_HAS_OBJECT_LIST)  == 0)  return childP;
    if (strcmp(childP->name, LD_VOCAB_HAS_JSON)         == 0)  return childP;
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// ldToSimplified -
//
bool ldToSimplified(KjNode* entityP, KAlloc* faP)
{
  if (entityP == NULL || entityP->type != KjObject)
    return false;

  KjNode* childP = entityP->value.firstChildP;

  while (childP != NULL)
  {
    KjNode* nextP = childP->next;

    //
    // § 5.3.2.4 "Multi-Attribute Representation": a multi-attribute does NOT
    // simplify to its bare value like a single-instance one - it becomes
    //
    //     "speed": { "dataset": { "@none": 55, "urn:ngsi-ld:ds:1": 54.5 } }
    //
    // keyed by datasetId, with the default instance under the JSON-LD keyword
    // "@none" (annex C.2.2.4.2). Leaving the array alone shipped the fully
    // NORMALIZED instances - type, value key and all - in a simplified response.
    //
    if (ldIsEntityKeyword(childP->name) == false && childP->type == KjArray)
    {
      KjNode* datasetMap = kjObject(NULL, "dataset");

      for (KjNode* instP = childP->value.firstChildP; instP != NULL; instP = instP->next)
      {
        if (instP->type != KjObject)
          continue;

        KjNode* valueP = ldAttrValueNode(instP);
        if (valueP == NULL)
          continue;

        KjNode*     dsP   = kjLookup(instP, "datasetId");
        const char* dsKey = (dsP != NULL && dsP->type == KjString) ? dsP->value.s : "@none";

        kjChildRemove(instP, valueP);

        if ((strcmp(valueP->name, LD_VOCAB_HAS_LANGUAGE_MAP) == 0) ||
            (strcmp(valueP->name, LD_VOCAB_HAS_VOCAB)        == 0) ||
            (strcmp(valueP->name, LD_VOCAB_HAS_JSON)         == 0))
        {
          // Same carve-out as the single-instance path below: these three keep
          // their { languageMap | vocab | json : ... } wrapper in simplified form.
          KjNode* wrapP = kjObject(NULL, dsKey);
          kjChildAdd(wrapP, valueP);
          kjChildAdd(datasetMap, wrapP);
        }
        else
        {
          valueP->name = (char*) dsKey;
          kjChildAdd(datasetMap, valueP);
        }
      }

      childP->type              = KjObject;
      childP->value.firstChildP = NULL;
      childP->lastChild         = NULL;
      kjChildAdd(childP, datasetMap);

      childP = nextP;
      continue;
    }

    if (ldIsEntityKeyword(childP->name) == false && childP->type == KjObject)
    {
      // § 4.5.23 + § 4.5.4: join=inline attaches the linked Entity under
      // `entity` on the Relationship instance. In simplified format with
      // join, the value of the Relationship is the inlined Entity (itself
      // simplified) — not the URI in `object`.
      KjNode* entityValP = kjLookup(childP, "entity");
      if (entityValP != NULL && entityValP->type == KjObject)
      {
        ldToSimplified(entityValP, faP);
        childP->type  = entityValP->type;
        childP->value = entityValP->value;
        childP = nextP;
        continue;
      }
      if (entityValP != NULL && entityValP->type == KjArray)
      {
        // Multivalued join (§ C.2.2.1.2): the Relationship's value becomes the
        // ARRAY of inlined Entities, each itself simplified — not the object URIs.
        for (KjNode* linkedP = entityValP->value.firstChildP; linkedP != NULL; linkedP = linkedP->next)
          ldToSimplified(linkedP, faP);
        childP->type  = entityValP->type;
        childP->value = entityValP->value;
        childP = nextP;
        continue;
      }

      KjNode* valueP = ldAttrValueNode(childP);

      if (valueP != NULL)
      {
        if ((strcmp(valueP->name, LD_VOCAB_HAS_LANGUAGE_MAP) == 0) ||
            (strcmp(valueP->name, LD_VOCAB_HAS_VOCAB)        == 0) ||
            (strcmp(valueP->name, LD_VOCAB_HAS_JSON)         == 0))
        {
          // § 5.2.6.4: LanguageProperty / VocabProperty / JsonProperty keep the
          // { languageMap | vocab | json : value } wrapper in simplified form.
          // Drop "type" and any sub-attrs, keeping just the value node.
          valueP->next              = NULL;
          childP->value.firstChildP = valueP;
          childP->lastChild         = valueP;
        }
        else
        {
          // Property / GeoProperty / Relationship / List* reduce to the bare value.
          childP->type  = valueP->type;
          childP->value = valueP->value;
        }
      }
    }

    childP = nextP;
  }

  KLOG_T(LdTRender, "Entity converted to simplified format");
  return true;
}
