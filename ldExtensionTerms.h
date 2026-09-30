#ifndef CORNGSILD_LDEXTENSIONTERMS_H_
#define CORNGSILD_LDEXTENSIONTERMS_H_

//
// FILE            ldExtensionTerms.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include "corAlloc/CorAlloc.h"                           // CorAlloc



// -----------------------------------------------------------------------------
//
// ldExtensionTermsAdd - make the terms of coraine's NGSI-LD extensions CORE terms
//
// A member of a Query body or a Subscription that the core context does not define would be
// expanded with the client's @context (@vocab) - and would then be nothing the broker recognizes.
// These are added to the core at startup (corLdCoreTermsAdd), as the ContextBridge terms are, and
// each has its CorTerm id. Call after corLdInit, before ldCoreTermIdsInit.
//
//   langProperties  - the attributes that are LanguageProperties, for q (spec-doubts #134)
//
// Returns 0, or -1 on error.
//
extern int ldExtensionTermsAdd(CorAlloc* kaP);

#endif  // CORNGSILD_LDEXTENSIONTERMS_H_
