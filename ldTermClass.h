#ifndef CORNGSILD_LD_TERM_CLASS_H_
#define CORNGSILD_LD_TERM_CLASS_H_

//
// FILE            ldTermClass.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include "corNgsild/CorTerm.h"                          // CorTermLast



// -----------------------------------------------------------------------------
//
// LD_TC_* - the groups a core term belongs to, as bits
//
// "Is this one of {id, type, scope, ...}?" is then one table load and one AND, and a
// group is defined in ONE place (ldTermClass.c) - a member added to it reaches every
// caller at once, instead of the lists in ten files that drifted apart.
//
#define LD_TC_ENTITY_MEMBER   0x01   // a member of an Entity that is not an Attribute
#define LD_TC_VALUE_KEY       0x02   // the member of an Attribute that holds its value (value, object, languageMap, ...)
#define LD_TC_SUBATTR_ONLY    0x10   // a name only valid as a member of an Attribute, never as an Attribute (observedAt, unitCode)
#define LD_TC_SYS_ATTR        0x20   // a system attribute stripped without sysAttrs (createdAt, modifiedAt)
#define LD_TC_WELL_KNOWN_GEO  0x40   // an Attribute name that must be a GeoProperty (location, observationSpace, operationSpace)



// -----------------------------------------------------------------------------
//
// ldTermClass - LD_TC_* bits of each CorTerm, indexed by it (CorTermNone: 0)
//
extern const unsigned char ldTermClass[CorTermLast];

#endif  // CORNGSILD_LD_TERM_CLASS_H_
