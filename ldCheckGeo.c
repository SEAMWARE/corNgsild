//
// FILE            ldCheckGeo.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strcmp

#include "kbase/kLibLog.h"                             // KLOG_T
#include "kjson/KjNode.h"                               // KjNode

#include "swNgsild/LdProblem.h"                          // LD_ERROR_*
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_COORDINATES, LD_VOCAB_GEO_*
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldCheckGeo.h"                         // Own interface
#include "swNgsild/ldTraceLevels.h"                      // LdTCheckGeo



// -----------------------------------------------------------------------------
//
// geoError - call ldError with "Invalid GeoJSON" and return false
//
static bool geoError(const char* detail)
{
  ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid GeoJSON", "%s", detail);
  return false;
}



// -----------------------------------------------------------------------------
//
// nodeNumberValue - extract numeric value from a KjNode (int or float)
//
static bool nodeNumberValue(KjNode* nodeP, double* valueP)
{
  if (nodeP->type == KjFloat)  { *valueP = nodeP->value.f;           return true; }
  if (nodeP->type == KjInt)    { *valueP = (double) nodeP->value.i;  return true; }
  return false;
}



// -----------------------------------------------------------------------------
//
// childCount - count children of a container node
//
static int childCount(KjNode* containerP)
{
  int count = 0;
  for (KjNode* childP = containerP->value.firstChildP; childP != NULL; childP = childP->next)
    ++count;
  return count;
}



// -----------------------------------------------------------------------------
//
// checkPosition - validate a single position [lon, lat] or [lon, lat, alt]
//
static bool checkPosition(KjNode* posP)
{
  if (posP == NULL || posP->type != KjArray)
    return geoError("GeoJSON position must be an array");

  int count = childCount(posP);
  if (count < 2 || count > 3)
    return geoError("GeoJSON position must have 2 or 3 elements (lon, lat[, alt])");

  KjNode*  lonP = posP->value.firstChildP;
  KjNode*  latP = lonP->next;
  double   lon, lat;

  if (nodeNumberValue(lonP, &lon) == false)  return geoError("GeoJSON longitude must be a number");
  if (nodeNumberValue(latP, &lat) == false)  return geoError("GeoJSON latitude must be a number");
  if (lon < -180.0 || lon > 180.0)          return geoError("GeoJSON longitude must be in range [-180, 180]");
  if (lat < -90.0 || lat > 90.0)            return geoError("GeoJSON latitude must be in range [-90, 90]");

  if (count == 3)
  {
    double alt;
    if (nodeNumberValue(latP->next, &alt) == false)
      return geoError("GeoJSON altitude must be a number");
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// checkLineString - validate an array of positions (at least 2)
//
static bool checkLineString(KjNode* coordsP)
{
  if (coordsP == NULL || coordsP->type != KjArray)
    return geoError("GeoJSON LineString coordinates must be an array");

  if (childCount(coordsP) < 2)
    return geoError("GeoJSON LineString must have at least 2 positions");

  for (KjNode* posP = coordsP->value.firstChildP; posP != NULL; posP = posP->next)
  {
    if (checkPosition(posP) == false)
      return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// positionsEqual - check if two position arrays have the same coordinates
//
static bool positionsEqual(KjNode* pos1P, KjNode* pos2P)
{
  KjNode*  c1 = pos1P->value.firstChildP;
  KjNode*  c2 = pos2P->value.firstChildP;

  while (c1 != NULL && c2 != NULL)
  {
    double v1, v2;
    if (nodeNumberValue(c1, &v1) == false || nodeNumberValue(c2, &v2) == false)
      return false;
    if (v1 != v2)
      return false;
    c1 = c1->next;
    c2 = c2->next;
  }

  return (c1 == NULL && c2 == NULL) ? true : false;
}



// -----------------------------------------------------------------------------
//
// checkLinearRing - validate a polygon ring (closed LineString with >= 4 points)
//
static bool checkLinearRing(KjNode* ringP)
{
  if (ringP == NULL || ringP->type != KjArray)
    return geoError("GeoJSON polygon ring must be an array");

  if (childCount(ringP) < 4)
    return geoError("GeoJSON polygon ring must have at least 4 positions");

  KjNode*  firstP = NULL;
  KjNode*  lastP  = NULL;

  for (KjNode* posP = ringP->value.firstChildP; posP != NULL; posP = posP->next)
  {
    if (checkPosition(posP) == false)
      return false;
    if (firstP == NULL)
      firstP = posP;
    lastP = posP;
  }

  if (positionsEqual(firstP, lastP) == false)
    return geoError("GeoJSON polygon ring must be closed (first == last position)");

  return true;
}



// -----------------------------------------------------------------------------
//
// checkPolygonCoords - validate polygon coordinates (array of linear rings)
//
static bool checkPolygonCoords(KjNode* coordsP)
{
  if (coordsP == NULL || coordsP->type != KjArray)
    return geoError("GeoJSON Polygon coordinates must be an array");

  if (childCount(coordsP) < 1)
    return geoError("GeoJSON Polygon must have at least one ring");

  for (KjNode* ringP = coordsP->value.firstChildP; ringP != NULL; ringP = ringP->next)
  {
    if (checkLinearRing(ringP) == false)
      return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// ldCheckGeo -
//
// Expects a GeoJSON geometry object with "type" and "coordinates" (expanded).
// After JSON-LD expansion, "type" stays as "type" (expands to @type, skipped)
// and "coordinates" becomes "https://purl.org/geojson/vocab#coordinates".
//
bool ldCheckGeo(KjNode* geoValueP)
{
  if (geoValueP == NULL || geoValueP->type != KjObject)
    return geoError("GeoJSON value must be an object");

  KjNode*  typeP   = NULL;
  KjNode*  coordsP = NULL;

  for (KjNode* childP = geoValueP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, "type") == 0)                typeP   = childP;
    else if (strcmp(childP->name, LD_VOCAB_COORDINATES) == 0)  coordsP = childP;
  }

  if (typeP == NULL || typeP->type != KjString)
    return geoError("GeoJSON must have a 'type' string field");

  if (coordsP == NULL || coordsP->type != KjArray)
    return geoError("GeoJSON must have a 'coordinates' array field");

  const char* geoType = typeP->value.s;

  // After JSON-LD expansion, GeoJSON type values may be expanded to full IRIs.
  // Accept both short and expanded forms.

  if (strcmp(geoType, "Point") == 0 || strcmp(geoType, LD_VOCAB_GEO_POINT) == 0)
    return checkPosition(coordsP);

  if (strcmp(geoType, "LineString") == 0 || strcmp(geoType, LD_VOCAB_GEO_LINE_STRING) == 0)
    return checkLineString(coordsP);

  if (strcmp(geoType, "Polygon") == 0 || strcmp(geoType, LD_VOCAB_GEO_POLYGON) == 0)
    return checkPolygonCoords(coordsP);

  if (strcmp(geoType, "MultiPoint") == 0 || strcmp(geoType, LD_VOCAB_GEO_MULTI_POINT) == 0)
  {
    for (KjNode* posP = coordsP->value.firstChildP; posP != NULL; posP = posP->next)
    {
      if (checkPosition(posP) == false)
        return false;
    }
    return true;
  }

  if (strcmp(geoType, "MultiLineString") == 0 || strcmp(geoType, LD_VOCAB_GEO_MULTI_LINE) == 0)
  {
    for (KjNode* lineP = coordsP->value.firstChildP; lineP != NULL; lineP = lineP->next)
    {
      if (checkLineString(lineP) == false)
        return false;
    }
    return true;
  }

  if (strcmp(geoType, "MultiPolygon") == 0 || strcmp(geoType, LD_VOCAB_GEO_MULTI_POLYGON) == 0)
  {
    for (KjNode* polyP = coordsP->value.firstChildP; polyP != NULL; polyP = polyP->next)
    {
      if (checkPolygonCoords(polyP) == false)
        return false;
    }
    return true;
  }

  ldError(400, LD_ERROR_BAD_REQUEST_DATA, "Invalid GeoJSON", "Unknown GeoJSON geometry type: '%s'", geoType);
  return false;
}
