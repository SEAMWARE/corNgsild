#ifndef CORNGSILD_LDRENDER_H_
#define CORNGSILD_LDRENDER_H_

//
// FILE            ldRender.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode



// -----------------------------------------------------------------------------
//
// ldToNormalized -
//
extern bool ldToNormalized(KjNode* entityP, KAlloc* faP);



// -----------------------------------------------------------------------------
//
// ldToConcise -
//
extern bool ldToConcise(KjNode* entityP, KAlloc* faP);



// -----------------------------------------------------------------------------
//
// ldToSimplified -
//
extern bool ldToSimplified(KjNode* entityP, KAlloc* faP);



// -----------------------------------------------------------------------------
//
// ldAttrValueNode - the value-holding member of a normalized/concise attribute
//                   (value / object / languageMap / vocab / valueList /
//                   objectList / json); NULL if none.
//
extern KjNode* ldAttrValueNode(KjNode* attrP);

#endif  // CORNGSILD_LDRENDER_H_
