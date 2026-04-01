#ifndef SWNGSILD_LDLANGREDUCE_H_
#define SWNGSILD_LDLANGREDUCE_H_

//
// FILE            ldLangReduce.h
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
// ldLangReduce - reduce LanguageProperty attributes to Property with matching language
//
extern void ldLangReduce(KjNode* entityP, const char* lang, KAlloc* faP);

#endif  // SWNGSILD_LDLANGREDUCE_H_
