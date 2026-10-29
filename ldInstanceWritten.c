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
#include <stdint.h>                                   // int64_t

#include "corRest/corRest.h"                          // corRest
#include "corTree/CorNode.h"                          // CorNode
#include "corTree/corTreeArraySort.h"                 // corTreeNodeCompare
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

  if (preModP->value.i != postModP->value.i)
    return true;

  //
  // The same modifiedAt: carried over - unless it is THIS request's time, which every write of the request
  // is given: a batch that names the Entity more than once writes the instance in an earlier fragment, and
  // this one may write it again. Then what the instance holds tells (§ 10.3.3.3: each fragment its own).
  // An instance of an earlier request is told by its time alone, at no cost
  //
  if (postModP->value.i != (int64_t) corRest.requestStartTime)
    return false;

  return (corTreeNodeCompare(preP, postP) != 0);
}
