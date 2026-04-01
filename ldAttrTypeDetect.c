//
// FILE            ldAttrTypeDetect.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <string.h>                                      // strcmp

#include "kbase/kLibLog.h"                             // KLOG_T
#include "kjson/KjNode.h"                               // KjNode

#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/ldTypes.h"                            // ldAttrTypeFromString, ldAttrTypeToString
#include "swNgsild/ldAttrTypeDetect.h"                   // Own interface
#include "swNgsild/ldTraceLevels.h"                      // LdTDetect



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
LdAttrType ldAttrTypeDetect(KjNode* attrP)
{
  if (attrP == NULL)
    return LdAttrNone;

  // If it's not an object, it could be simplified format (plain value)
  if (attrP->type != KjObject)
    return LdAttrNone;

  // Walk the children looking for "type" or value keys
  LdAttrType detected = LdAttrNone;

  for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, "type") == 0)
    {
      if (childP->type == KjString)
      {
        LdAttrType fromType = ldAttrTypeFromString(childP->value.s);
        if (fromType != LdAttrNone)
        {
          KLOG_T(LdTDetect, "Detected type '%s' from explicit type field", childP->value.s);
          return fromType;
        }
      }
    }
  }

  // No explicit type - infer from value key (expanded IRIs)
  for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if      (strcmp(childP->name, LD_VOCAB_HAS_VALUE)        == 0)  detected = LdAttrProperty;
    else if (strcmp(childP->name, LD_VOCAB_HAS_OBJECT)       == 0)  detected = LdAttrRelationship;
    else if (strcmp(childP->name, LD_VOCAB_HAS_LANGUAGE_MAP) == 0)  detected = LdAttrLanguageProperty;
    else if (strcmp(childP->name, LD_VOCAB_HAS_VOCAB)        == 0)  detected = LdAttrVocabProperty;
    else if (strcmp(childP->name, LD_VOCAB_HAS_VALUE_LIST)   == 0)  detected = LdAttrListProperty;
    else if (strcmp(childP->name, LD_VOCAB_HAS_OBJECT_LIST)  == 0)  detected = LdAttrListRelationship;
    else if (strcmp(childP->name, LD_VOCAB_HAS_JSON)         == 0)  detected = LdAttrJsonProperty;

    if (detected != LdAttrNone)
    {
      KLOG_T(LdTDetect, "Detected type '%s' from value key '%s'",
           ldAttrTypeToString(detected), childP->name);
      return detected;
    }
  }

  return LdAttrNone;
}
