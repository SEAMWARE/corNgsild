//
// FILE            ldTypes.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <string.h>                                      // strcmp

#include "kbase/kLibLog.h"                             // KLOG_T

#include "swNgsild/LdVocab.h"                            // LD_VOCAB_HAS_*
#include "swNgsild/ldTypes.h"                            // Own interface
#include "swNgsild/ldTraceLevels.h"                      // LdTTypes



// -----------------------------------------------------------------------------
//
// ldValueKeyForType - return the expanded IRI value key for an attribute type
//
const char* ldValueKeyForType(LdAttrType attrType)
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
// ldAttrTypeToString -
//
const char* ldAttrTypeToString(LdAttrType attrType)
{
  switch (attrType)
  {
  case LdAttrNone:             return "None";
  case LdAttrProperty:         return "Property";
  case LdAttrRelationship:     return "Relationship";
  case LdAttrGeoProperty:      return "GeoProperty";
  case LdAttrLanguageProperty: return "LanguageProperty";
  case LdAttrVocabProperty:    return "VocabProperty";
  case LdAttrListProperty:     return "ListProperty";
  case LdAttrListRelationship: return "ListRelationship";
  case LdAttrJsonProperty:     return "JsonProperty";
  }

  return "Unknown";
}



// -----------------------------------------------------------------------------
//
// ldAttrTypeFromString -
//
LdAttrType ldAttrTypeFromString(const char* str)
{
  if (str == NULL)
    return LdAttrNone;

  //
  // Short names (pre-expansion)
  //
  if (strcmp(str, "Property")         == 0)  return LdAttrProperty;
  if (strcmp(str, "Relationship")     == 0)  return LdAttrRelationship;
  if (strcmp(str, "GeoProperty")      == 0)  return LdAttrGeoProperty;
  if (strcmp(str, "LanguageProperty") == 0)  return LdAttrLanguageProperty;
  if (strcmp(str, "VocabProperty")    == 0)  return LdAttrVocabProperty;
  if (strcmp(str, "ListProperty")     == 0)  return LdAttrListProperty;
  if (strcmp(str, "ListRelationship") == 0)  return LdAttrListRelationship;
  if (strcmp(str, "JsonProperty")     == 0)  return LdAttrJsonProperty;

  //
  // Expanded URIs (after JSON-LD expansion of @type values)
  //
  if (strcmp(str, "https://uri.etsi.org/ngsi-ld/Property")         == 0)  return LdAttrProperty;
  if (strcmp(str, "https://uri.etsi.org/ngsi-ld/Relationship")     == 0)  return LdAttrRelationship;
  if (strcmp(str, "https://uri.etsi.org/ngsi-ld/GeoProperty")      == 0)  return LdAttrGeoProperty;
  if (strcmp(str, "https://uri.etsi.org/ngsi-ld/LanguageProperty") == 0)  return LdAttrLanguageProperty;
  if (strcmp(str, "https://uri.etsi.org/ngsi-ld/VocabProperty")    == 0)  return LdAttrVocabProperty;
  if (strcmp(str, "https://uri.etsi.org/ngsi-ld/ListProperty")     == 0)  return LdAttrListProperty;
  if (strcmp(str, "https://uri.etsi.org/ngsi-ld/ListRelationship") == 0)  return LdAttrListRelationship;
  if (strcmp(str, "https://uri.etsi.org/ngsi-ld/JsonProperty")     == 0)  return LdAttrJsonProperty;

  return LdAttrNone;
}



// -----------------------------------------------------------------------------
//
// ldOpToString -
//
const char* ldOpToString(LdOp op)
{
  switch (op)
  {
  case LdOpNone:               return "None";
  case LdOpCreateEntity:       return "CreateEntity";
  case LdOpUpdateEntity:       return "UpdateEntity";
  case LdOpAppendAttrs:        return "AppendAttrs";
  case LdOpMergeEntity:        return "MergeEntity";
  case LdOpReplaceEntity:      return "ReplaceEntity";
  case LdOpDeleteEntity:       return "DeleteEntity";
  case LdOpDeleteAttr:         return "DeleteAttr";
  case LdOpReplaceAttr:        return "ReplaceAttr";
  case LdOpPurgeEntity:        return "PurgeEntity";
  case LdOpBatchCreate:        return "BatchCreate";
  case LdOpBatchUpsert:        return "BatchUpsert";
  case LdOpBatchUpdate:        return "BatchUpdate";
  case LdOpBatchDelete:        return "BatchDelete";
  case LdOpBatchMerge:         return "BatchMerge";
  case LdOpCreateSubscription: return "CreateSubscription";
  case LdOpUpdateSubscription: return "UpdateSubscription";
  case LdOpCreateRegistration: return "CreateRegistration";
  case LdOpUpdateRegistration: return "UpdateRegistration";
  case LdOpRetrieveEntity:     return "RetrieveEntity";
  case LdOpQueryEntities:      return "QueryEntities";
  }

  return "Unknown";
}



// -----------------------------------------------------------------------------
//
// ldFormatToString -
//
const char* ldFormatToString(LdFormat format)
{
  switch (format)
  {
  case LdFormatNone:       return "None";
  case LdFormatNormalized: return "normalized";
  case LdFormatConcise:    return "concise";
  case LdFormatSimplified: return "simplified";
  }

  return "Unknown";
}



// -----------------------------------------------------------------------------
//
// ldFormatFromString -
//
LdFormat ldFormatFromString(const char* str)
{
  if (str == NULL)
    return LdFormatNone;

  if (strcmp(str, "normalized") == 0)  return LdFormatNormalized;
  if (strcmp(str, "concise")    == 0)  return LdFormatConcise;
  if (strcmp(str, "simplified") == 0)  return LdFormatSimplified;
  if (strcmp(str, "keyValues")  == 0)  return LdFormatSimplified;

  return LdFormatNone;
}
