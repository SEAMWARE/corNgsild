#ifndef SWNGSILD_LDCHECKENTITY_H_
#define SWNGSILD_LDCHECKENTITY_H_

//
// FILE            ldCheckEntity.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "swNgsild/LdOp.h"                               // LdOp



// -----------------------------------------------------------------------------
//
// ldCheckEntity -
//
extern bool ldCheckEntity(KjNode* entityP, LdOp op, KjNode* dbEntityP,
                           KAlloc* faP);

#endif  // SWNGSILD_LDCHECKENTITY_H_
