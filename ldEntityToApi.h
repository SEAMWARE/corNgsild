#ifndef SWNGSILD_LDENTITYTOAPI_H_
#define SWNGSILD_LDENTITYTOAPI_H_

//
// FILE            ldEntityToApi.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode



// -----------------------------------------------------------------------------
//
// ldEntityToApi - transform storage-format entity tree to API-format
//
// Unwraps dataset-keyed objects back to NGSI-LD API format:
//   single @none key  -> plain object (no datasetId)
//   multiple keys     -> array with datasetId fields restored
//
extern void ldEntityToApi(KjNode* entityP, KAlloc* faP);

#endif  // SWNGSILD_LDENTITYTOAPI_H_
