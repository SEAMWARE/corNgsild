//
// FILE            ldInstanceWritten.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                  // bool
#include <stddef.h>                                   // NULL

#include "kjson/KjNode.h"                             // KjNode
#include "kjson/kjLookup.h"                           // kjLookup

#include "corNgsild/LdVocab.h"                        // LD_VOCAB_MODIFIED_AT
#include "corNgsild/ldInstanceWritten.h"              // Own interface



// -----------------------------------------------------------------------------
//
// ldInstanceWritten -
//
bool ldInstanceWritten(KjNode* preAttrP, KjNode* postAttrP, const char* dsKey)
{
  KjNode* preP  = (preAttrP  != NULL) ? kjLookup(preAttrP,  dsKey) : NULL;
  KjNode* postP = (postAttrP != NULL) ? kjLookup(postAttrP, dsKey) : NULL;

  if ((preP == NULL) || (postP == NULL))
    return (preP != postP);

  KjNode* preModP  = kjLookup(preP,  LD_VOCAB_MODIFIED_AT);
  KjNode* postModP = kjLookup(postP, LD_VOCAB_MODIFIED_AT);

  if ((preModP == NULL) || (postModP == NULL) || (preModP->type != KjInt) || (postModP->type != KjInt))
    return true;  // nothing to tell them apart - a write to the Attribute counts, as for a plain name

  return (preModP->value.i != postModP->value.i);
}
