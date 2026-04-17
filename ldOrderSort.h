#ifndef SWNGSILD_LDORDERSORT_H_
#define SWNGSILD_LDORDERSORT_H_

//
// FILE            ldOrderSort.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include "kjson/KjNode.h"                               // KjNode
#include "swNgsild/LdOrder.h"                            // LdOrderTerm



// ldOrderSort - sort an entity array in-place per orderBy terms
extern void ldOrderSort(KjNode* arrayP, LdOrderTerm* terms, int termCount);

#endif  // SWNGSILD_LDORDERSORT_H_
