//
// FILE            ldRender.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool
#include "swRest/swRest.h"                            // swRest
#include <string.h>                                      // strcmp

#include "kbase/kLibLog.h"                             // KLOG_T
#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjBuilder.h"                             // kjString

#include "swNgsild/LdAttrType.h"                         // LdAttrType
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/ldTypes.h"                            // ldAttrTypeToString
#include "swNgsild/ldAttrTypeDetect.h"                   // ldAttrTypeDetect
#include "swNgsild/ldRender.h"                           // Own interface
#include "swNgsild/ldTraceLevels.h"                      // LdTRender



// -----------------------------------------------------------------------------
//
// isEntityKeyword -
//
static bool isEntityKeyword(const char* name)
{
  if (strcmp(name, "id")            == 0)  return true;
  if (strcmp(name, "@id")           == 0)  return true;
  if (strcmp(name, "type")          == 0)  return true;
  if (strcmp(name, "@type")         == 0)  return true;
  if (strcmp(name, "@context")      == 0)  return true;
  if (strcmp(name, LD_VOCAB_SCOPE)  == 0)  return true;

  return false;
}



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
    KjNode* typeNodeP = kjString(swRest.kjsonP, "type", ldAttrTypeToString(attrType));
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
    if (isEntityKeyword(childP->name) == false)
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
    if (isEntityKeyword(childP->name) == false)
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
// getValueNode - find the value node for a normalized/concise attribute
//
static KjNode* getValueNode(KjNode* attrP)
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

    if (isEntityKeyword(childP->name) == false && childP->type == KjObject)
    {
      KjNode* valueP = getValueNode(childP);

      if (valueP != NULL)
      {
        // Replace the attribute object with just the value
        childP->type  = valueP->type;
        childP->value = valueP->value;
      }
    }

    childP = nextP;
  }

  KLOG_T(LdTRender, "Entity converted to simplified format");
  return true;
}
