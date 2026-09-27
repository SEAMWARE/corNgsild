#ifndef CORNGSILD_LDRENDER_H_
#define CORNGSILD_LDRENDER_H_

//
// FILE            ldRender.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include <stdbool.h>                                     // bool

#include "kalloc/KAlloc.h"                             // KAlloc
#include "corTree/CorNode.h"                            // CorNode



// -----------------------------------------------------------------------------
//
// ldToConcise -
//
extern bool ldToConcise(CorNode* entityP, KAlloc* faP);



// -----------------------------------------------------------------------------
//
// ldToSimplified -
//
extern bool ldToSimplified(CorNode* entityP, KAlloc* faP);



// -----------------------------------------------------------------------------
//
// ldAttrValueNode - the value-holding member of a normalized/concise attribute
//                   (value / object / languageMap / vocab / valueList /
//                   objectList / json); NULL if none.
//
extern CorNode* ldAttrValueNode(CorNode* attrP);

#endif  // CORNGSILD_LDRENDER_H_
