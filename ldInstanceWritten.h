#ifndef CORNGSILD_LD_INSTANCE_WRITTEN_H_
#define CORNGSILD_LD_INSTANCE_WRITTEN_H_

//
// FILE            ldInstanceWritten.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                  // bool

#include "kjson/KjNode.h"                             // KjNode



// -----------------------------------------------------------------------------
//
// ldInstanceWritten - was the instance dsKey of an Attribute written by a change?
//
// preAttrP is the Attribute before the change (a merge report's preValue - NULL
// for attributeCreated) and postAttrP after it (NULL for attributeDeleted), both
// dataset-keyed. An instance that appeared or went away was written; one that is
// in both was written if its modifiedAt moved - every instance a write touches
// is stamped, the others keep theirs.
//
// The ONE answer to "which instances did this write touch" - a subscription
// watching attr@datasetId and the temporal history both ask it.
//
extern bool ldInstanceWritten(KjNode* preAttrP, KjNode* postAttrP, const char* dsKey);

#endif  // CORNGSILD_LD_INSTANCE_WRITTEN_H_
