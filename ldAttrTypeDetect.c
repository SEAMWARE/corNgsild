//
// FILE            ldAttrTypeDetect.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include <string.h>                                      // strcmp

#include "corBase/corLibLog.h"                         // COR_LIB_*
#include "corTree/CorNode.h"                            // CorNode

#include "corNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "corNgsild/ldTypes.h"                            // ldAttrTypeFromString, ldAttrTypeToString
#include "corNgsild/ldAttrTypeDetect.h"                   // Own interface
#include "corNgsild/ldTraceLevels.h"                      // LdTDetect
#include "corNgsild/ldTermId.h"                           // ldTermId, CorTerm*



// -----------------------------------------------------------------------------
//
// ldAttrTypeDetect -
//
// If the attribute is an object, look for:
//   1. Explicit "type" field -> use ldAttrTypeFromString
//   2. Otherwise, infer from the value key present (expanded IRIs):
//      - hasValue       -> Property
//      - hasObject      -> Relationship
//      - hasLanguageMap -> LanguageProperty
//      - hasVocab       -> VocabProperty
//      - hasValueList   -> ListProperty
//      - hasObjectList  -> ListRelationship
//      - hasJSON        -> JsonProperty
//
// For GeoProperty, the type must be explicit (its value key is also hasValue,
// same as Property). GeoProperty detection from value key alone is not possible
// without also checking the GeoJSON structure, which is a validation concern.
//
LdAttrType ldAttrTypeDetect(CorNode* attrP)
{
  if (attrP == NULL)
    return LdAttrNone;

  // If it's not an object, it could be simplified format (plain value)
  if (attrP->type != CorObject)
    return LdAttrNone;

  // Walk the children looking for "type" or value keys
  LdAttrType detected = LdAttrNone;

  for (CorNode* childP = attrP->value.head; childP != NULL; childP = childP->next)
  {
    if (ldTermId(childP) == CorTermType)
    {
      if (childP->type == CorString)
      {
        LdAttrType fromType = ldAttrTypeFromString(childP->value.s);
        if (fromType != LdAttrNone)
        {
          COR_LIB_T(LdTDetect, "Detected type '%s' from explicit type field", childP->value.s);
          return fromType;
        }
      }
    }
  }

  // No explicit type - infer from value key (expanded IRIs)
  for (CorNode* childP = attrP->value.head; childP != NULL; childP = childP->next)
  {
    if      (ldTermId(childP) == CorTermValue)  detected = LdAttrProperty;
    else if (ldTermId(childP) == CorTermObject)       detected = LdAttrRelationship;
    else if (ldTermId(childP) == CorTermLanguageMap)  detected = LdAttrLanguageProperty;
    else if (ldTermId(childP) == CorTermVocab)        detected = LdAttrVocabProperty;
    else if (ldTermId(childP) == CorTermValueList)    detected = LdAttrListProperty;
    else if (ldTermId(childP) == CorTermObjectList)   detected = LdAttrListRelationship;
    else if (ldTermId(childP) == CorTermJson)         detected = LdAttrJsonProperty;

    if (detected != LdAttrNone)
    {
      COR_LIB_T(LdTDetect, "Detected type '%s' from value key '%s'",
           ldAttrTypeToString(detected), childP->name);
      return detected;
    }
  }

  return LdAttrNone;
}
