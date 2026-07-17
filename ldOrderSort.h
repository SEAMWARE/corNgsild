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
//
// 'collation' is the § 7.6.2.3 collation= parameter (a BCP-47 tag, NULL = the
// § 7.6.2.1 default "root" order). It only affects string ordering: an ICU
// build honours it via the root/locale collator; a non-ICU build ignores it
// and applies a case-insensitive ASCII approximation of root collation.
extern void ldOrderSort(KjNode* arrayP, LdOrderTerm* terms, int termCount, const char* collation);

#endif  // SWNGSILD_LDORDERSORT_H_
