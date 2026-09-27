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
#include <string.h>                                      // strcmp

#include "kbase/kLibLog.h"                             // KLOG_T
#include "kalloc/KAlloc.h"                             // KAlloc
#include "corTree/CorNode.h"                             // CorNode
#include "corTree/corTreeBuilder.h"                      // corTreeChildAdd, corTreeChildRemove, corTreeObject
#include "corTree/corTreeLookup.h"                       // corTreeLookup

#include "corNgsild/LdAttrType.h"                         // LdAttrType
#include "corNgsild/LdVocab.h"                            // LD_VOCAB_*
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
// toConcise - remove "type" when it can be inferred from the value key
//
// =============================================================================



// -----------------------------------------------------------------------------
//
// attrToConcise -
//
static void attrToConcise(CorNode* attrP)
{
  if (attrP->type != CorObject)
    return;

  LdAttrType attrType = ldAttrTypeDetect(attrP);
  if (attrType == LdAttrNone)
    return;

  // Remove "type" if it can be inferred
  if (attrTypeCanBeInferred(attrType) == true)
  {
    CorNode* prevP = NULL;
    for (CorNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
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
  for (CorNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
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
    CorNode* valueP    = NULL;
    bool     valueOnly = true;

    for (CorNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
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
bool ldToConcise(CorNode* entityP, KAlloc* faP)
{
  if (entityP == NULL || entityP->type != CorObject)
    return false;

  for (CorNode* childP = entityP->value.firstChildP; childP != NULL; childP = childP->next)
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
    if (childP->type == CorArray)
    {
      for (CorNode* instP = childP->value.firstChildP; instP != NULL; instP = instP->next)
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
CorNode* ldAttrValueNode(CorNode* attrP)
{
  if (attrP->type != CorObject)
    return NULL;

  for (CorNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
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
bool ldToSimplified(CorNode* entityP, KAlloc* faP)
{
  if (entityP == NULL || entityP->type != CorObject)
    return false;

  //
  // The multi-attribute branch below is the only part of this file that CREATES
  // nodes, and faP is here for exactly that. It used to pass NULL, which makes
  // the builders fall back to malloc - so every simplified multi-attribute response
  // leaked its dataset wrapper for the life of the process.
  //
  KAlloc* allocP = faP;

  CorNode* childP = entityP->value.firstChildP;

  while (childP != NULL)
  {
    CorNode* nextP = childP->next;

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
    if (ldIsEntityKeyword(childP->name) == false && childP->type == CorArray)
    {
      CorNode* datasetMap = corTreeObject(allocP, "dataset");

      for (CorNode* instP = childP->value.firstChildP; instP != NULL; instP = instP->next)
      {
        if (instP->type != CorObject)
          continue;

        CorNode* valueP = ldAttrValueNode(instP);
        if (valueP == NULL)
          continue;

        CorNode*    dsP   = corTreeLookup(instP, "datasetId");
        const char* dsKey = (dsP != NULL && dsP->type == CorString) ? dsP->value.s : "@none";

        corTreeChildRemove(instP, valueP);

        if ((strcmp(valueP->name, LD_VOCAB_HAS_LANGUAGE_MAP) == 0) ||
            (strcmp(valueP->name, LD_VOCAB_HAS_VOCAB)        == 0) ||
            (strcmp(valueP->name, LD_VOCAB_HAS_JSON)         == 0))
        {
          // Same carve-out as the single-instance path below: these three keep
          // their { languageMap | vocab | json : ... } wrapper in simplified form.
          CorNode* wrapP = corTreeObject(allocP, dsKey);
          corTreeChildAdd(wrapP, valueP);
          corTreeChildAdd(datasetMap, wrapP);
        }
        else
        {
          valueP->name = (char*) dsKey;
          corTreeChildAdd(datasetMap, valueP);
        }
      }

      childP->type              = CorObject;
      childP->value.firstChildP = NULL;
      childP->lastChild         = NULL;
      corTreeChildAdd(childP, datasetMap);

      childP = nextP;
      continue;
    }

    if (ldIsEntityKeyword(childP->name) == false && childP->type == CorObject)
    {
      // § 4.5.23 + § 4.5.4: join=inline attaches the linked Entity under
      // `entity` on the Relationship instance. In simplified format with
      // join, the value of the Relationship is the inlined Entity (itself
      // simplified) — not the URI in `object`.
      CorNode* entityValP = corTreeLookup(childP, "entity");
      if (entityValP != NULL && entityValP->type == CorObject)
      {
        ldToSimplified(entityValP, faP);
        childP->type  = entityValP->type;
        childP->value = entityValP->value;
        childP = nextP;
        continue;
      }
      if (entityValP != NULL && entityValP->type == CorArray)
      {
        // Multivalued join (§ C.2.2.1.2): the Relationship's value becomes the
        // ARRAY of inlined Entities, each itself simplified — not the object URIs.
        for (CorNode* linkedP = entityValP->value.firstChildP; linkedP != NULL; linkedP = linkedP->next)
          ldToSimplified(linkedP, faP);
        childP->type  = entityValP->type;
        childP->value = entityValP->value;
        childP = nextP;
        continue;
      }

      CorNode* valueP = ldAttrValueNode(childP);

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
