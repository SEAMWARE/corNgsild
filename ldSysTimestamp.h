#ifndef CORNGSILD_LD_SYS_TIMESTAMP_H
#define CORNGSILD_LD_SYS_TIMESTAMP_H

// -----------------------------------------------------------------------------
//
// FILE            ldSysTimestamp.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// TS 104-176 § 6.4.5 — system-generated createdAt/modifiedAt timestamps for
// the Subscription and Registration NGSI-LD Elements. Stored in the resource
// tree as epoch-nanosecond integers (same representation as entity sysattrs,
// so a future temporal filter can compare them numerically), rendered to ISO
// 8601 strings only when the client asks for system attributes.
//
#include "kjson/KjNode.h"                                // KjNode
#include "kalloc/KAlloc.h"                               // KAlloc



// -----------------------------------------------------------------------------
//
// ldSysTimestampToIso - epoch nanoseconds → "2026-04-01T12:00:00.123Z"
//
extern void ldSysTimestampToIso(long long nsec, char* buf, int bufSize);



// -----------------------------------------------------------------------------
//
// ldSysTimestampsToIso - convert a tree's top-level createdAt/modifiedAt
//                        integer members to ISO 8601 strings, in place
//
extern void ldSysTimestampsToIso(KjNode* treeP, KAlloc* allocP);



// -----------------------------------------------------------------------------
//
// ldSysTimestampCreate - stamp createdAt AND modifiedAt = now (request time)
//                        onto a resource tree as nanosecond integers
//
extern void ldSysTimestampCreate(KjNode* treeP);



// -----------------------------------------------------------------------------
//
// ldSysTimestampModify - set/replace modifiedAt = now on a resource tree
//                        (createdAt, if present, is left untouched)
//
extern void ldSysTimestampModify(KjNode* treeP);

#endif  // CORNGSILD_LD_SYS_TIMESTAMP_H
