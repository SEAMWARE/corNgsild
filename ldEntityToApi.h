#ifndef CORNGSILD_LDENTITYTOAPI_H_
#define CORNGSILD_LDENTITYTOAPI_H_

//
// FILE            ldEntityToApi.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corTree/CorNode.h"                            // CorNode



// -----------------------------------------------------------------------------
//
// ldEntityToApi - transform storage-format entity tree to API-format
//
// Unwraps dataset-keyed objects back to NGSI-LD API format:
//   single @none key  -> plain object (no datasetId)
//   multiple keys     -> array with datasetId fields restored
//
extern void ldEntityToApi(CorNode* entityP, CorAlloc* faP);

#endif  // CORNGSILD_LDENTITYTOAPI_H_
