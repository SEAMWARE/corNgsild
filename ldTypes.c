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
  case LdOpNone:                      return "None";
  case LdOpCreateEntity:              return "CreateEntity";
  case LdOpUpdateEntity:              return "UpdateEntity";
  case LdOpAppendAttrs:               return "AppendAttrs";
  case LdOpUpdateAttrs:               return "UpdateAttrs";
  case LdOpMergeEntity:               return "MergeEntity";
  case LdOpReplaceEntity:             return "ReplaceEntity";
  case LdOpDeleteEntity:              return "DeleteEntity";
  case LdOpDeleteAttr:                return "DeleteAttr";
  case LdOpReplaceAttr:               return "ReplaceAttr";
  case LdOpPurgeEntity:               return "PurgeEntity";
  case LdOpRetrieveEntity:            return "RetrieveEntity";
  case LdOpQueryEntities:             return "QueryEntities";
  case LdOpBatchCreate:               return "BatchCreate";
  case LdOpBatchUpsert:               return "BatchUpsert";
  case LdOpBatchUpdate:               return "BatchUpdate";
  case LdOpBatchDelete:               return "BatchDelete";
  case LdOpBatchMerge:                return "BatchMerge";
  case LdOpBatchQuery:                return "BatchQuery";
  case LdOpRetrieveEntityTypes:       return "RetrieveEntityTypes";
  case LdOpRetrieveEntityTypeDetails: return "RetrieveEntityTypeDetails";
  case LdOpRetrieveEntityTypeInfo:    return "RetrieveEntityTypeInfo";
  case LdOpRetrieveAttrTypes:         return "RetrieveAttrTypes";
  case LdOpRetrieveAttrTypeDetails:   return "RetrieveAttrTypeDetails";
  case LdOpRetrieveAttrTypeInfo:      return "RetrieveAttrTypeInfo";
  case LdOpCreateSubscription:        return "CreateSubscription";
  case LdOpUpdateSubscription:        return "UpdateSubscription";
  case LdOpRetrieveSubscription:      return "RetrieveSubscription";
  case LdOpQuerySubscription:         return "QuerySubscription";
  case LdOpDeleteSubscription:        return "DeleteSubscription";
  case LdOpCreateRegistration:        return "CreateRegistration";
  case LdOpUpdateRegistration:        return "UpdateRegistration";
  case LdOpRetrieveRegistration:      return "RetrieveRegistration";
  case LdOpQueryRegistration:         return "QueryRegistration";
  case LdOpDeleteRegistration:        return "DeleteRegistration";
  case LdOpCreateCsourceSubscription:   return "CreateCsourceSubscription";
  case LdOpUpdateCsourceSubscription:   return "UpdateCsourceSubscription";
  case LdOpRetrieveCsourceSubscription: return "RetrieveCsourceSubscription";
  case LdOpQueryCsourceSubscription:    return "QueryCsourceSubscription";
  case LdOpDeleteCsourceSubscription:   return "DeleteCsourceSubscription";
  case LdOpUpsertTemporal:                  return "UpsertTemporal";
  case LdOpAppendAttrsTemporal:             return "AppendAttrsTemporal";
  case LdOpDeleteAttrsTemporal:             return "DeleteAttrsTemporal";
  case LdOpUpdateAttrInstanceTemporal:      return "UpdateAttrInstanceTemporal";
  case LdOpDeleteAttrInstanceTemporal:      return "DeleteAttrInstanceTemporal";
  case LdOpDeleteTemporal:                  return "DeleteTemporal";
  case LdOpRetrieveTemporal:                return "RetrieveTemporal";
  case LdOpQueryTemporal:                   return "QueryTemporal";
  case LdOpCreateEntityMapQueryTemporal:    return "CreateEntityMapQueryTemporal";
  case LdOpCreateSnapshot:                  return "CreateSnapshot";
  case LdOpCloneSnapshot:                   return "CloneSnapshot";
  case LdOpRetrieveSnapshot:                return "RetrieveSnapshot";
  case LdOpUpdateSnapshot:                  return "UpdateSnapshot";
  case LdOpDeleteSnapshot:                  return "DeleteSnapshot";
  case LdOpPurgeSnapshots:                  return "PurgeSnapshots";
  }

  return "Unknown";
}



// -----------------------------------------------------------------------------
//
// ldOpFromName - map an NGSI-LD op string to its LdOp bit.
//
// Spec-defined atomic op names as they appear in CSourceRegistration's
// operations[] field. Returns LdOpNone on unknown name.
//
LdOp ldOpFromName(const char* name)
{
  if (name == NULL)
    return LdOpNone;

  // Entity write
  if (strcmp(name, "createEntity")              == 0)  return LdOpCreateEntity;
  if (strcmp(name, "updateEntity")              == 0)  return LdOpUpdateEntity;
  if (strcmp(name, "appendAttrs")               == 0)  return LdOpAppendAttrs;
  if (strcmp(name, "updateAttrs")               == 0)  return LdOpUpdateAttrs;
  if (strcmp(name, "mergeEntity")               == 0)  return LdOpMergeEntity;
  if (strcmp(name, "replaceEntity")             == 0)  return LdOpReplaceEntity;
  if (strcmp(name, "deleteEntity")              == 0)  return LdOpDeleteEntity;
  if (strcmp(name, "deleteAttrs")               == 0)  return LdOpDeleteAttr;
  if (strcmp(name, "deleteAttr")                == 0)  return LdOpDeleteAttr;
  if (strcmp(name, "replaceAttrs")              == 0)  return LdOpReplaceAttr;
  if (strcmp(name, "replaceAttr")               == 0)  return LdOpReplaceAttr;
  if (strcmp(name, "purgeEntity")               == 0)  return LdOpPurgeEntity;

  // Entity read
  if (strcmp(name, "retrieveEntity")            == 0)  return LdOpRetrieveEntity;
  if (strcmp(name, "queryEntity")               == 0)  return LdOpQueryEntities;

  // Batch
  if (strcmp(name, "createBatch")               == 0)  return LdOpBatchCreate;
  if (strcmp(name, "upsertBatch")               == 0)  return LdOpBatchUpsert;
  if (strcmp(name, "updateBatch")               == 0)  return LdOpBatchUpdate;
  if (strcmp(name, "deleteBatch")               == 0)  return LdOpBatchDelete;
  if (strcmp(name, "mergeBatch")                == 0)  return LdOpBatchMerge;
  if (strcmp(name, "queryBatch")                == 0)  return LdOpBatchQuery;

  // Type / attribute-type metadata
  if (strcmp(name, "retrieveEntityTypes")       == 0)  return LdOpRetrieveEntityTypes;
  if (strcmp(name, "retrieveEntityTypeDetails") == 0)  return LdOpRetrieveEntityTypeDetails;
  if (strcmp(name, "retrieveEntityTypeInfo")    == 0)  return LdOpRetrieveEntityTypeInfo;
  if (strcmp(name, "retrieveAttrTypes")         == 0)  return LdOpRetrieveAttrTypes;
  if (strcmp(name, "retrieveAttrTypeDetails")   == 0)  return LdOpRetrieveAttrTypeDetails;
  if (strcmp(name, "retrieveAttrTypeInfo")      == 0)  return LdOpRetrieveAttrTypeInfo;

  // Subscription
  if (strcmp(name, "createSubscription")        == 0)  return LdOpCreateSubscription;
  if (strcmp(name, "updateSubscription")        == 0)  return LdOpUpdateSubscription;
  if (strcmp(name, "retrieveSubscription")      == 0)  return LdOpRetrieveSubscription;
  if (strcmp(name, "querySubscription")         == 0)  return LdOpQuerySubscription;
  if (strcmp(name, "deleteSubscription")        == 0)  return LdOpDeleteSubscription;

  // Temporal API (§ 4.20 Table 4.20-1, § 5.6.11-§ 5.6.16, § 5.7.3-§ 5.7.4)
  if (strcmp(name, "upsertTemporal")              == 0)  return LdOpUpsertTemporal;
  if (strcmp(name, "appendAttrsTemporal")         == 0)  return LdOpAppendAttrsTemporal;
  if (strcmp(name, "deleteAttrsTemporal")         == 0)  return LdOpDeleteAttrsTemporal;
  if (strcmp(name, "updateAttrInstanceTemporal")  == 0)  return LdOpUpdateAttrInstanceTemporal;
  if (strcmp(name, "deleteAttrInstanceTemporal")  == 0)  return LdOpDeleteAttrInstanceTemporal;
  if (strcmp(name, "deleteTemporal")              == 0)  return LdOpDeleteTemporal;
  if (strcmp(name, "retrieveTemporal")            == 0)  return LdOpRetrieveTemporal;
  if (strcmp(name, "queryTemporal")               == 0)  return LdOpQueryTemporal;
  if (strcmp(name, "createEntityMapQueryTemporal") == 0) return LdOpCreateEntityMapQueryTemporal;

  return LdOpNone;
}



// -----------------------------------------------------------------------------
//
// ldOpGroupMaskFromName - map a § 4.20 group name to its OR-mask.
//
// Returns 0 on unknown name. Intended fall-through after ldOpFromName()
// for CSR operations[] entries.
//
uint64_t ldOpGroupMaskFromName(const char* name)
{
  if (name == NULL)
    return 0;

  if (strcmp(name, "federationOps")  == 0)  return LD_OP_GROUP_FEDERATION;
  if (strcmp(name, "associationOps") == 0)  return LD_OP_GROUP_ASSOCIATION;
  if (strcmp(name, "retrieveOps")    == 0)  return LD_OP_GROUP_RETRIEVE;
  if (strcmp(name, "updateOps")      == 0)  return LD_OP_GROUP_UPDATE;
  if (strcmp(name, "redirectionOps") == 0)  return LD_OP_GROUP_REDIRECTION;

  return 0;
}



// -----------------------------------------------------------------------------
//
// ldFormatToString -
//
const char* ldFormatToString(LdFormat format)
{
  switch (format)
  {
  case LdFormatNone:             return "None";
  case LdFormatNormalized:       return "normalized";
  case LdFormatConcise:          return "concise";
  case LdFormatSimplified:       return "simplified";
  case LdFormatTemporalValues:   return "temporalValues";
  case LdFormatAggregatedValues: return "aggregatedValues";
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

  if (strcmp(str, "normalized")        == 0)  return LdFormatNormalized;
  if (strcmp(str, "concise")           == 0)  return LdFormatConcise;
  if (strcmp(str, "simplified")        == 0)  return LdFormatSimplified;
  if (strcmp(str, "keyValues")         == 0)  return LdFormatSimplified;
  if (strcmp(str, "temporalValues")    == 0)  return LdFormatTemporalValues;
  if (strcmp(str, "aggregatedValues")  == 0)  return LdFormatAggregatedValues;

  return LdFormatNone;
}
