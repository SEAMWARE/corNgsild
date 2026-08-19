#ifndef CORNGSILD_LDTOGEOJSON_H_
#define CORNGSILD_LDTOGEOJSON_H_

//
// FILE            ldToGeoJson.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include "kjson/KjNode.h"
#include "kjson/kjson.h"

extern void ldToGeoJson(KjNode** treePP, const char* geometryProperty, Kjson* kjsonP);

#endif  // CORNGSILD_LDTOGEOJSON_H_
