#ifndef SWNGSILD_LDPICKOMIT_H_
#define SWNGSILD_LDPICKOMIT_H_

//
// FILE            ldPickOmit.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include "kjson/KjNode.h"                           // KjNode



// -----------------------------------------------------------------------------
//
// ldPickOmit - apply pick/omit attribute projection to an entity
//
// If pickV is non-NULL, only attributes whose (expanded) name appears in pickV
// are kept.  If omitV is non-NULL, attributes whose name appears in omitV are
// removed.  The special fields "id", "type", and "@context" are never removed.
//
extern void ldPickOmit(KjNode* entityP, char** pickV, char** omitV);

#endif  // SWNGSILD_LDPICKOMIT_H_
