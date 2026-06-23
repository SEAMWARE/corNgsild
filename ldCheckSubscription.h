#ifndef SWNGSILD_LDCHECKSUBSCRIPTION_H_
#define SWNGSILD_LDCHECKSUBSCRIPTION_H_

//
// FILE            ldCheckSubscription.h
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
// ldCheckSubscription -
//
extern bool ldCheckSubscription(KjNode* subP, LdOp op, KAlloc* faP);



// -----------------------------------------------------------------------------
//
// ldSubEntityTypeExprsRelease - free the per-request entity-type-expr scratch
// (per-thread swNgsild side-channel). Called from the broker's post-response
// hook so the buffer is released per request rather than leaked at thread exit.
//
extern void ldSubEntityTypeExprsRelease(void);

#endif  // SWNGSILD_LDCHECKSUBSCRIPTION_H_
