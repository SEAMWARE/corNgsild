#ifndef SWNGSILD_LDQPARSE_H_
#define SWNGSILD_LDQPARSE_H_

//
// FILE            ldQParse.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include "kalloc/KAlloc.h"                             // KAlloc
#include "swNgsild/LdQ.h"                               // LdQNode



// -----------------------------------------------------------------------------
//
// ldQParse - parse a ?q= expression into an expression tree
//
// Returns a kaP-allocated LdQNode tree, or NULL on parse error (ldError called).
//
extern LdQNode* ldQParse(const char* q, KAlloc* kaP);

#endif  // SWNGSILD_LDQPARSE_H_
