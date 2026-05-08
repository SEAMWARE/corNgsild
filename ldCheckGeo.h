#ifndef SWNGSILD_LDCHECKGEO_H_
#define SWNGSILD_LDCHECKGEO_H_

//
// FILE            ldCheckGeo.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool

#include "kjson/KjNode.h"                               // KjNode



// -----------------------------------------------------------------------------
//
// ldCheckGeo -
//
extern bool ldCheckGeo(KjNode* geoValueP);



// -----------------------------------------------------------------------------
//
// ldCheckGeoQuery - validate the URL-param geometry+coordinates pair
//
// `geometry`    : "Point", "Polygon", "LineString", etc. (already
//                 type-validated by ldUrlParams)
// `coordinates` : raw JSON array as a NUL-terminated string (e.g.
//                 "[[[lon,lat], ...]]")
//
// Parses the coordinates JSON, builds a `{type, coordinates}` GeoJSON
// tree, and runs the full ldCheckGeo validation chain (range, ring
// closure, self-intersection, ...). On failure ldError is already
// set; the caller short-circuits its own pipeline.
//
// Returns true on success, false on validation failure.
//
extern bool ldCheckGeoQuery(const char* geometry, const char* coordinates);

#endif  // SWNGSILD_LDCHECKGEO_H_
