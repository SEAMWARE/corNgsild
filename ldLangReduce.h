#ifndef CORNGSILD_LDLANGREDUCE_H_
#define CORNGSILD_LDLANGREDUCE_H_

//
// FILE            ldLangReduce.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include "kalloc/KAlloc.h"                             // KAlloc
#include "corTree/CorNode.h"                            // CorNode



// -----------------------------------------------------------------------------
//
// ldLangReduce - reduce LanguageProperty attributes to Property with matching language
//
extern void ldLangReduce(CorNode* entityP, const char* lang, KAlloc* faP);

#endif  // CORNGSILD_LDLANGREDUCE_H_
