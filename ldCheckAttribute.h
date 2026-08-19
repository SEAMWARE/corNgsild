#ifndef CORNGSILD_LDCHECKATTRIBUTE_H_
#define CORNGSILD_LDCHECKATTRIBUTE_H_

//
// FILE            ldCheckAttribute.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include <stdbool.h>                                     // bool

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "corNgsild/LdAttrType.h"                         // LdAttrType
#include "corNgsild/LdOp.h"                               // LdOp



// -----------------------------------------------------------------------------
//
// ldCheckAttribute -
//
extern bool ldCheckAttribute(KjNode* attrP, LdOp op,
                              LdAttrType attrTypeFromDb,
                              KAlloc* faP);

#endif  // CORNGSILD_LDCHECKATTRIBUTE_H_
