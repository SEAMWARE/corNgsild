#ifndef CORNGSILD_LDCHECKSUBSCRIPTION_H_
#define CORNGSILD_LDCHECKSUBSCRIPTION_H_

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
#include "corNgsild/LdOp.h"                               // LdOp
#include "corNgsild/LdFormat.h"                           // LdFormat



// -----------------------------------------------------------------------------
//
// ldCheckSubscription -
//
// 'merged' true ⇒ validating the COMPLETE merged result of a PATCH (§ 5.8.3):
// enforce the same mandatory/consistency rules as Create, but tolerate the
// server-owned read-only fields (status, …) the stored document carries.
//
// notifFormatP (optional, may be NULL): on success, receives the parsed
// notification.format so the caller can hand it to ldSubCacheItemAdd without
// re-matching the string (LdFormatNone when no 'format' member is present).
extern bool ldCheckSubscription(KjNode* subP, LdOp op, bool merged, LdFormat* notifFormatP, KAlloc* faP);



// -----------------------------------------------------------------------------
//
// ldSubEntityTypeExprsRelease - free the per-request entity-type-expr scratch
// (per-thread corNgsild side-channel). Called from the broker's post-response
// hook so the buffer is released per request rather than leaked at thread exit.
//
extern void ldSubEntityTypeExprsRelease(void);

#endif  // CORNGSILD_LDCHECKSUBSCRIPTION_H_
