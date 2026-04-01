#ifndef SWNGSILD_LDNORMALIZEINPUT_H_
#define SWNGSILD_LDNORMALIZEINPUT_H_

//
// FILE            LdNormalizeInput.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode



// -----------------------------------------------------------------------------
//
// ldNormalizeInput - convert simplified/concise attributes to normalized format
//
// Called after JSON-LD expansion in ldParseHook.  Walks the entity tree and
// ensures every attribute is an KjObject with an explicit "type" field and
// the appropriate value key (hasValue, hasObject, etc.).
//
extern void ldNormalizeInput(KjNode* entityP, KAlloc* faP);

#endif  // SWNGSILD_LDNORMALIZEINPUT_H_
