#ifndef CORNGSILD_LDCHECKENTITY_H_
#define CORNGSILD_LDCHECKENTITY_H_

//
// FILE            ldCheckEntity.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include <stdbool.h>                                     // bool

#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corTree/CorNode.h"                            // CorNode
#include "corNgsild/LdOp.h"                               // LdOp



// -----------------------------------------------------------------------------
//
// ldCheckEntity -
//
extern bool ldCheckEntity(CorNode* entityP, LdOp op, CorNode* dbEntityP,
                           CorAlloc* faP);

#endif  // CORNGSILD_LDCHECKENTITY_H_
