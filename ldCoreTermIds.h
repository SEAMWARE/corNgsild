#ifndef CORNGSILD_LD_CORE_TERM_IDS_H_
#define CORNGSILD_LD_CORE_TERM_IDS_H_

//
// FILE            ldCoreTermIds.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include "corAlloc/CorAlloc.h"                       // CorAlloc

#include "corNgsild/CorTerm.h"                       // CorTerm, CorTermLast



// -----------------------------------------------------------------------------
//
// ldCoreTermNameV - the name of each CorTerm, indexed by it (ldCoreTermNameV[0] is NULL)
//
extern const char* ldCoreTermNameV[CorTermLast];



// -----------------------------------------------------------------------------
//
// ldCoreTermIdsInit - give every core-context item its CorTerm
//
// Call once at startup, after corLdInit and after the Bridge/Channel terms are added.
// Returns -1 if a core term has no CorTerm (the broker must not start).
//
extern int ldCoreTermIdsInit(CorAlloc* kaP);

#endif  // CORNGSILD_LD_CORE_TERM_IDS_H_
