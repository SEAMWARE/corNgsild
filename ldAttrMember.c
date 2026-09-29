//
// FILE            ldAttrMember.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdint.h>                                      // uint32_t

#include "corTree/CorNode.h"                            // CorNode
#include "corTree/corTreeBuilder.h"                     // corTreeChildRemove

#include "corNgsild/CorTerm.h"                          // CorTerm*
#include "corNgsild/LdAttrType.h"                       // LdAttrType
#include "corNgsild/ldIsEntityKeyword.h"                // ldIsNotAttribute
#include "corNgsild/ldAttrMember.h"                     // Own interface



// -----------------------------------------------------------------------------
//
// ldAttrMemberBit -
//
const uint32_t ldAttrMemberBit[CorTermLast] =
{
  [CorTermType]                 = LD_AM_TYPE,
  [CorTermValue]                = LD_AM_VALUE,
  [CorTermObject]               = LD_AM_OBJECT,
  [CorTermObjectList]           = LD_AM_OBJECT_LIST,
  [CorTermLanguageMap]          = LD_AM_LANGUAGE_MAP,
  [CorTermVocab]                = LD_AM_VOCAB,
  [CorTermValueList]            = LD_AM_VALUE_LIST,
  [CorTermJson]                 = LD_AM_JSON,
  [CorTermObservedAt]           = LD_AM_OBSERVED_AT,
  [CorTermUnitCode]             = LD_AM_UNIT_CODE,
  [CorTermDatasetId]            = LD_AM_DATASET_ID,
  [CorTermValueType]            = LD_AM_VALUE_TYPE,
  [CorTermObjectType]           = LD_AM_OBJECT_TYPE,
  [CorTermExpiresAt]            = LD_AM_EXPIRES_AT,
  [CorTermCreatedAt]            = LD_AM_CREATED_AT,
  [CorTermModifiedAt]           = LD_AM_MODIFIED_AT,
  [CorTermDeletedAt]            = LD_AM_DELETED_AT,
  [CorTermInstanceId]           = LD_AM_INSTANCE_ID,
  [CorTermPreviousValue]        = LD_AM_PREVIOUS_VALUE,
  [CorTermPreviousObject]       = LD_AM_PREVIOUS_OBJECT,
  [CorTermPreviousObjectList]   = LD_AM_PREVIOUS_OBJECT_LIST,
  [CorTermPreviousLanguageMap]  = LD_AM_PREVIOUS_LANGUAGE_MAP,
  [CorTermPreviousVocab]        = LD_AM_PREVIOUS_VOCAB,
  [CorTermPreviousValueList]    = LD_AM_PREVIOUS_VALUE_LIST,
  [CorTermPreviousJson]         = LD_AM_PREVIOUS_JSON,
  [CorTermNgsildproof]          = LD_AM_NGSILDPROOF,
  [CorTermEntity]               = LD_AM_ENTITY,
  [CorTermEntityList]           = LD_AM_ENTITY_LIST,
  [CorTermLang]                 = LD_AM_LANG,
};



//
// What a CLIENT may send on any Attribute, and per type on top of that. This is what
// ldCheckAttribute's isAllowedCoreAttrTerm allowed (valueType on the Property family,
// unitCode on Property/ListProperty, objectType on the relationships), plus expiresAt
// and ngsildproof, which it had left to the sub-attribute path.
//
#define IN_COMMON      (LD_AM_TYPE | LD_AM_OBSERVED_AT | LD_AM_DATASET_ID | LD_AM_EXPIRES_AT | LD_AM_NGSILDPROOF)
#define IN_PROPERTY    (IN_COMMON | LD_AM_VALUE        | LD_AM_UNIT_CODE | LD_AM_VALUE_TYPE)
#define IN_REL         (IN_COMMON | LD_AM_OBJECT       | LD_AM_OBJECT_TYPE)
#define IN_GEO         (IN_COMMON | LD_AM_VALUE        | LD_AM_VALUE_TYPE)
#define IN_LANG        (IN_COMMON | LD_AM_LANGUAGE_MAP | LD_AM_VALUE_TYPE)
#define IN_VOCAB       (IN_COMMON | LD_AM_VOCAB        | LD_AM_VALUE_TYPE)
#define IN_LIST        (IN_COMMON | LD_AM_VALUE_LIST   | LD_AM_UNIT_CODE | LD_AM_VALUE_TYPE)
#define IN_LIST_REL    (IN_COMMON | LD_AM_OBJECT_LIST  | LD_AM_OBJECT_TYPE)
#define IN_JSON        (IN_COMMON | LD_AM_JSON         | LD_AM_VALUE_TYPE)
#define IN_ANY         (IN_PROPERTY | IN_REL | IN_GEO | IN_LANG | IN_VOCAB | IN_LIST | IN_LIST_REL | IN_JSON)

#define ALL_MEMBERS    0xFFFFFFFFu

#define DROP_ENTITY    (LD_AM_CREATED_AT | LD_AM_MODIFIED_AT)
#define DROP_TEMPORAL  (LD_AM_CREATED_AT | LD_AM_MODIFIED_AT | LD_AM_DELETED_AT | LD_AM_INSTANCE_ID)



// -----------------------------------------------------------------------------
//
// ldAttrKeep - indexed [LdMemberSource][LdAttrType] (LdAttrNone: type still unknown - any type's)
//
const uint32_t ldAttrKeep[LdSourceCount][LdAttrJsonProperty + 1] =
{
  [LdSourceEntityRequest]   = { IN_ANY, IN_PROPERTY, IN_REL, IN_GEO, IN_LANG, IN_VOCAB, IN_LIST, IN_LIST_REL, IN_JSON },
  [LdSourceTemporalRequest] = { IN_ANY, IN_PROPERTY, IN_REL, IN_GEO, IN_LANG, IN_VOCAB, IN_LIST, IN_LIST_REL, IN_JSON },
  [LdSourceContextSource]   = { ALL_MEMBERS, ALL_MEMBERS, ALL_MEMBERS, ALL_MEMBERS, ALL_MEMBERS, ALL_MEMBERS, ALL_MEMBERS, ALL_MEMBERS, ALL_MEMBERS },
  [LdSourceDb]              = { ALL_MEMBERS, ALL_MEMBERS, ALL_MEMBERS, ALL_MEMBERS, ALL_MEMBERS, ALL_MEMBERS, ALL_MEMBERS, ALL_MEMBERS, ALL_MEMBERS },
};



// -----------------------------------------------------------------------------
//
// ldAttrDrop - indexed [LdMemberSource][LdAttrType]
//
const uint32_t ldAttrDrop[LdSourceCount][LdAttrJsonProperty + 1] =
{
  [LdSourceEntityRequest]   = { DROP_ENTITY,   DROP_ENTITY,   DROP_ENTITY,   DROP_ENTITY,   DROP_ENTITY,   DROP_ENTITY,   DROP_ENTITY,   DROP_ENTITY,   DROP_ENTITY   },
  [LdSourceTemporalRequest] = { DROP_TEMPORAL, DROP_TEMPORAL, DROP_TEMPORAL, DROP_TEMPORAL, DROP_TEMPORAL, DROP_TEMPORAL, DROP_TEMPORAL, DROP_TEMPORAL, DROP_TEMPORAL },
};



// -----------------------------------------------------------------------------
//
// ldAttrMembersStrip -
//
void ldAttrMembersStrip(CorNode* attrP, LdMemberSource source, LdAttrType attrType)
{
  if ((attrP == NULL) || (attrP->type != CorObject))
    return;

  uint32_t drop  = ldAttrDrop[source][attrType];
  CorNode* nextP;

  for (CorNode* childP = attrP->value.head; childP != NULL; childP = nextP)
  {
    nextP = childP->next;

    if ((ldAttrMemberOf(childP) & drop) != 0)
      corTreeChildRemove(attrP, childP);
  }
}



// -----------------------------------------------------------------------------
//
// ldTemporalMembersStrip -
//
void ldTemporalMembersStrip(CorNode* entityP)
{
  if ((entityP == NULL) || (entityP->type != CorObject))
    return;

  for (CorNode* attrP = entityP->value.head; attrP != NULL; attrP = attrP->next)
  {
    if (ldIsNotAttribute(attrP) == true)
      continue;

    if (attrP->type == CorObject)
      ldAttrMembersStrip(attrP, LdSourceTemporalRequest, LdAttrNone);
    else if (attrP->type == CorArray)
    {
      for (CorNode* instP = attrP->value.head; instP != NULL; instP = instP->next)
        ldAttrMembersStrip(instP, LdSourceTemporalRequest, LdAttrNone);
    }
  }
}
