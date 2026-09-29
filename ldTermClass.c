//
// FILE            ldTermClass.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include "corNgsild/CorTerm.h"                          // CorTerm*
#include "corNgsild/ldTermClass.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// ldTermClass -
//
// Designated initializers: a term not named here is 0 - in no group.
//
const unsigned char ldTermClass[CorTermLast] =
{
  [CorTermId]           = LD_TC_ENTITY_MEMBER,
  [CorTermType]         = LD_TC_ENTITY_MEMBER,
  [CorTermScope]        = LD_TC_ENTITY_MEMBER,
  [CorTermCreatedAt]    = LD_TC_ENTITY_MEMBER | LD_TC_SYS_ATTR,
  [CorTermModifiedAt]   = LD_TC_ENTITY_MEMBER | LD_TC_SYS_ATTR,
  [CorTermExpiresAt]    = LD_TC_ENTITY_MEMBER,

  [CorTermValue]        = LD_TC_VALUE_KEY,
  [CorTermObject]       = LD_TC_VALUE_KEY,
  [CorTermLanguageMap]  = LD_TC_VALUE_KEY,
  [CorTermVocab]        = LD_TC_VALUE_KEY,
  [CorTermValueList]    = LD_TC_VALUE_KEY,
  [CorTermObjectList]   = LD_TC_VALUE_KEY,
  [CorTermJson]         = LD_TC_VALUE_KEY,

  [CorTermObservedAt]   = LD_TC_SUBATTR_ONLY,
  [CorTermUnitCode]     = LD_TC_SUBATTR_ONLY,

  [CorTermLocation]         = LD_TC_WELL_KNOWN_GEO,
  [CorTermObservationSpace] = LD_TC_WELL_KNOWN_GEO,
  [CorTermOperationSpace]   = LD_TC_WELL_KNOWN_GEO,
};
