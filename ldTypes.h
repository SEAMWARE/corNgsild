#ifndef SWNGSILD_LDTYPES_H_
#define SWNGSILD_LDTYPES_H_

//
// FILE            ldTypes.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include "swNgsild/LdAttrType.h"                        // LdAttrType
#include "swNgsild/LdOp.h"                              // LdOp
#include "swNgsild/LdFormat.h"                           // LdFormat



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
extern LdFormat ldFormatFromString(const char* str);

#endif  // SWNGSILD_LDTYPES_H_
