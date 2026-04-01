#ifndef SWNGSILD_LDCHECKATTRIBUTE_H_
#define SWNGSILD_LDCHECKATTRIBUTE_H_

//
// FILE            ldCheckAttribute.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "swNgsild/LdAttrType.h"                         // LdAttrType
#include "swNgsild/LdOp.h"                               // LdOp



// -----------------------------------------------------------------------------
//
// ldCheckAttribute -
//
extern bool ldCheckAttribute(KjNode* attrP, LdOp op,
                              LdAttrType attrTypeFromDb,
                              KAlloc* faP);

#endif  // SWNGSILD_LDCHECKATTRIBUTE_H_
