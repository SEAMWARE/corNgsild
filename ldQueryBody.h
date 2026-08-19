#ifndef CORNGSILD_LDQUERYBODY_H_
#define CORNGSILD_LDQUERYBODY_H_

//
// FILE            ldQueryBody.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Shared translator for the § 5.2.23 Query object. Walks a Query body
// (POST /entityMaps, POST /entityOperations/query, and POST
// /temporal/entityOperations/query) and feeds each field to ldParamHook
// in the URL-param form it understands, so the downstream query
// pipeline sees the same state as if the client had sent URL params.
//
// Returns true on success, false on validation error (caller should
// return immediately — ldError has already been set on the response).
//
#include <stdbool.h>                                  // bool
#include "kjson/KjNode.h"                             // KjNode



extern bool ldQueryBodyToParams(KjNode* bodyP);

#endif  // CORNGSILD_LDQUERYBODY_H_
