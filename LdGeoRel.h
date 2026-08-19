#ifndef CORNGSILD_LDGEOREL_H_
#define CORNGSILD_LDGEOREL_H_

//
// FILE            LdGeoRel.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include "kalloc/KAlloc.h"                             // KAlloc



// -----------------------------------------------------------------------------
//
// LdGeoRelType - georel relationship types
//
typedef enum LdGeoRelType
{
  LdGeoNone = 0,
  LdGeoNear,
  LdGeoWithin,
  LdGeoContains,
  LdGeoOverlaps,
  LdGeoIntersects,
  LdGeoEquals,
  LdGeoDisjoint
} LdGeoRelType;



// -----------------------------------------------------------------------------
//
// LdGeoRel - parsed georel parameter
//
typedef struct LdGeoRel
{
  LdGeoRelType  rel;
  double        maxDistance;   // -1 = not set
  double        minDistance;   // -1 = not set
} LdGeoRel;



// -----------------------------------------------------------------------------
//
// ldGeoRelParse - parse a georel string like "near;maxDistance==1000"
//
extern LdGeoRel* ldGeoRelParse(const char* georelStr, KAlloc* faP);

#endif  // CORNGSILD_LDGEOREL_H_
