//
// FILE            ldExtensionTerms.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stddef.h>                                      // NULL

#include "corAlloc/CorAlloc.h"                           // CorAlloc
#include "corJsonld/corLdInit.h"                         // CorLdCoreTerm, corLdCoreTermsAdd
#include "corNgsild/ldExtensionTerms.h"                  // Own interface



// -----------------------------------------------------------------------------
//
// extensionTermV - the terms of coraine's NGSI-LD extensions (each with a CorTerm id, CorTerm.h)
//
// ⚠ A core term overrides every other context, so its name is taken from every vocabulary a client
// may load: names absent from the well-known ones (Smart Data Models, schema.org, SOSA/SSN, SAREF).
//
static const CorLdCoreTerm extensionTermV[] =
{
  { "langProperties",  NULL },   // spec-doubts #134 - a q [..] under these attributes is a language tag
  { NULL,              NULL }
};



// -----------------------------------------------------------------------------
//
// ldExtensionTermsAdd -
//
int ldExtensionTermsAdd(CorAlloc* kaP)
{
  return (corLdCoreTermsAdd(extensionTermV, kaP) < 0) ? -1 : 0;
}
