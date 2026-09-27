#ifndef CORNGSILD_LDTOGEOJSON_H_
#define CORNGSILD_LDTOGEOJSON_H_

//
// FILE            ldToGeoJson.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include "corAlloc/CorAlloc.h"              // CorAlloc
#include "corTree/CorNode.h"

extern void ldToGeoJson(CorNode** treePP, const char* geometryProperty, CorAlloc* allocP);

#endif  // CORNGSILD_LDTOGEOJSON_H_
