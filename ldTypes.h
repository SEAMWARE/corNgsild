#ifndef CORNGSILD_LDTYPES_H_
#define CORNGSILD_LDTYPES_H_

//
// FILE            ldTypes.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool

#include "corNgsild/LdAttrType.h"                        // LdAttrType
#include "corNgsild/LdOp.h"                              // LdOp
#include "corNgsild/LdFormat.h"                           // LdFormat



// -----------------------------------------------------------------------------
//
// ldValueKeyForType - return the expanded IRI value key for an attribute type
//
extern const char* ldValueKeyForType(LdAttrType attrType);



// -----------------------------------------------------------------------------
//
// ldAttrTypeToString -
//
extern const char* ldAttrTypeToString(LdAttrType attrType);



// -----------------------------------------------------------------------------
//
// ldAttrTypeFromString -
//
extern LdAttrType ldAttrTypeFromString(const char* str);



// -----------------------------------------------------------------------------
//
// ldOpToString -
//
extern const char* ldOpToString(LdOp op);



// -----------------------------------------------------------------------------
//
// ldFormatToString -
//
extern const char* ldFormatToString(LdFormat format);



// -----------------------------------------------------------------------------
//
// ldFormatFromString -
//
// 'temporal' selects which family of representations is valid: with it false
// only the entity formats (normalized / concise / simplified, keyValues being
// the synonym for simplified) are recognised; with it true the temporal-query
// formats (temporalValues / aggregatedValues) are too. An unrecognised string
// (for the selected family) returns LdFormatNone.
//
extern LdFormat ldFormatFromString(const char* str, bool temporal);

#endif  // CORNGSILD_LDTYPES_H_
