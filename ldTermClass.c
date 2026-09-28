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
  [CorTermId]         = LD_TC_ENTITY_MEMBER,
  [CorTermType]       = LD_TC_ENTITY_MEMBER,
  [CorTermScope]      = LD_TC_ENTITY_MEMBER,
  [CorTermCreatedAt]  = LD_TC_ENTITY_MEMBER,
  [CorTermModifiedAt] = LD_TC_ENTITY_MEMBER,
  [CorTermExpiresAt]  = LD_TC_ENTITY_MEMBER,
};
