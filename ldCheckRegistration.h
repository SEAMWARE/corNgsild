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
// 'merged' true ⇒ validating the COMPLETE merged result of a PATCH (§ 5.9.3):
// enforce the same mandatory/consistency rules as Create, but tolerate a stored
// expiresAt that has since elapsed (the create-only future-check is skipped) and
// the server-owned read-only fields a stored document carries.
//
extern bool ldCheckRegistration(KjNode* regP, LdOp op, bool merged, KAlloc* faP);

#endif  // SWNGSILD_LDCHECKREGISTRATION_H_
