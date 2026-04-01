#ifndef SWNGSILD_LDCHECKREGISTRATION_H_
#define SWNGSILD_LDCHECKREGISTRATION_H_

//
// FILE            ldCheckRegistration.h
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
// ldCheckRegistration -
//
extern bool ldCheckRegistration(KjNode* regP, LdOp op, KAlloc* faP);

#endif  // SWNGSILD_LDCHECKREGISTRATION_H_
