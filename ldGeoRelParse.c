//
// FILE            ldGeoRelParse.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdlib.h>                                      // strtod
#include <string.h>                                      // strcmp, strncmp, strchr

#include "kalloc/KAlloc.h"                             // kaAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc

#include "corNgsild/LdProblem.h"                          // LD_ERROR_BAD_REQUEST_DATA
#include "corNgsild/ldError.h"                            // ldError
#include "corNgsild/LdGeoRel.h"                          // Own interface



// -----------------------------------------------------------------------------
//
// ldGeoRelParse - parse a georel string like "near;maxDistance==1000"
//
// Format: <rel>[;<modifier>]
// Where <rel> is one of: near, within, contains, overlaps, intersects, equals, disjoint
// And <modifier> is: maxDistance==<number> or minDistance==<number>
//
LdGeoRel* ldGeoRelParse(const char* georelStr, KAlloc* faP)
{
  if (georelStr == NULL || georelStr[0] == 0)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query", "georel value is empty");
    return NULL;
  }

  LdGeoRel* geoRelP = (LdGeoRel*) kaAlloc(faP, sizeof(LdGeoRel));

  geoRelP->maxDistance = -1;
  geoRelP->minDistance = -1;

  // Find the semicolon separator (if any)
  const char* semi = strchr(georelStr, ';');
  int relLen = (semi != NULL) ? (int)(semi - georelStr) : (int) strlen(georelStr);

  if      (relLen == 4  && strncmp(georelStr, "near",       4)  == 0)  geoRelP->rel = LdGeoNear;
  else if (relLen == 6  && strncmp(georelStr, "within",     6)  == 0)  geoRelP->rel = LdGeoWithin;
  else if (relLen == 8  && strncmp(georelStr, "contains",   8)  == 0)  geoRelP->rel = LdGeoContains;
  else if (relLen == 8  && strncmp(georelStr, "overlaps",   8)  == 0)  geoRelP->rel = LdGeoOverlaps;
  else if (relLen == 10 && strncmp(georelStr, "intersects", 10) == 0)  geoRelP->rel = LdGeoIntersects;
  else if (relLen == 6  && strncmp(georelStr, "equals",     6)  == 0)  geoRelP->rel = LdGeoEquals;
  else if (relLen == 8  && strncmp(georelStr, "disjoint",   8)  == 0)  geoRelP->rel = LdGeoDisjoint;
  else
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query", "unknown georel: '%.*s'", relLen, georelStr);
    return NULL;
  }

  // Parse modifiers after the semicolon
  while (semi != NULL)
  {
    const char* mod = semi + 1;
    semi = strchr(mod, ';');

    int modLen = (semi != NULL) ? (int)(semi - mod) : (int) strlen(mod);

    if (modLen > 13 && strncmp(mod, "maxDistance==", 13) == 0)
    {
      char* end = NULL;
      geoRelP->maxDistance = strtod(mod + 13, &end);
      if (end == mod + 13)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query", "invalid distance value in georel");
        return NULL;
      }
      if (geoRelP->maxDistance < 0)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query", "distance must not be negative in georel");
        return NULL;
      }
    }
    else if (modLen > 13 && strncmp(mod, "minDistance==", 13) == 0)
    {
      char* end = NULL;
      geoRelP->minDistance = strtod(mod + 13, &end);
      if (end == mod + 13)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query", "invalid distance value in georel");
        return NULL;
      }
      if (geoRelP->minDistance < 0)
      {
        ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query", "distance must not be negative in georel");
        return NULL;
      }
    }
    else
    {
      ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query", "unknown georel modifier: '%.*s'", modLen, mod);
      return NULL;
    }
  }

  // 'near' requires at least one distance modifier
  if (geoRelP->rel == LdGeoNear && geoRelP->maxDistance < 0 && geoRelP->minDistance < 0)
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query", "georel 'near' requires maxDistance or minDistance");
    return NULL;
  }

  // Distance modifiers only valid with 'near'
  if (geoRelP->rel != LdGeoNear && (geoRelP->maxDistance >= 0 || geoRelP->minDistance >= 0))
  {
    ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid geo-query", "maxDistance/minDistance only valid with georel 'near'");
    return NULL;
  }

  return geoRelP;
}
