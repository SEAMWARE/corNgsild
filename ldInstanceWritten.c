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

#include "corTree/CorNode.h"                          // CorNode
#include "corTree/corTreeLookup.h"                    // corTreeLookup

#include "corNgsild/LdVocab.h"                        // LD_VOCAB_MODIFIED_AT
#include "corNgsild/ldInstanceWritten.h"              // Own interface



// -----------------------------------------------------------------------------
//
// ldInstanceWritten -
//
bool ldInstanceWritten(CorNode* preAttrP, CorNode* postAttrP, const char* dsKey)
{
  CorNode* preP = (preAttrP  != NULL) ? corTreeLookup(preAttrP, dsKey) : NULL;
  CorNode* postP = (postAttrP != NULL) ? corTreeLookup(postAttrP, dsKey) : NULL;

  if ((preP == NULL) || (postP == NULL))
    return (preP != postP);

  CorNode* preModP = corTreeLookup(preP, LD_VOCAB_MODIFIED_AT);
  CorNode* postModP = corTreeLookup(postP, LD_VOCAB_MODIFIED_AT);

  if ((preModP == NULL) || (postModP == NULL) || (preModP->type != CorInt) || (postModP->type != CorInt))
    return true;  // nothing to tell them apart - a write to the Attribute counts, as for a plain name

  return (preModP->value.i != postModP->value.i);
}
