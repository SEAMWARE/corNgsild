//
// FILE            ldToGeoJson.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stddef.h>                                      // NULL
#include <string.h>                                      // strcmp

#include "kalloc/KAlloc.h"                               // KAlloc
#include "corTree/CorNode.h"                             // CorNode
#include "corTree/corTreeBuilder.h"                      // corTreeObject, corTreeString, corTreeArray, corTreeChildAdd, corTreeNull
#include "corTree/corTreeLookup.h"                       // corTreeLookup
#include "corTree/corTreeClone.h"                        // corTreeClone

#include "corNgsild/CorNgsild.h"                           // corNgsild (geoJsonGeomForced)
#include "corNgsild/ldToGeoJson.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// extractGeometry - extract GeoJSON geometry from an entity's GeoProperty
//
// Two shapes seen at this point:
//   normalized:  "location": { "type": "GeoProperty", "value": { "type": "Point", "coordinates": [...] } }
//   simplified:  "location": { "type": "Point", "coordinates": [...] }      (after keyValues collapse)
//
// In simplified the wrapping GeoProperty has already been stripped — the
// attr is the geometry. Distinguish by looking for a `value` sub-object;
// if absent and the attr itself has GeoJSON shape (`type` is one of the
// geometry types + `coordinates`), use the attr directly.
//
static CorNode* extractGeometry(CorNode* entityP, const char* geoPropName, KAlloc* allocP)
{
  CorNode* attrP = corTreeLookup(entityP, geoPropName);

  if (attrP == NULL)
    return NULL;

  // Multi-instance GeoProperty (§ 5.3.3.2): with no datasetId in the request the
  // attribute arrives here as an array of instances. Select the DEFAULT instance
  // (the one without a datasetId); if there is no default, the geometry is
  // undefined. A request datasetId has already collapsed the array to one object.
  if (attrP->type == CorArray)
  {
    CorNode* defaultInstanceP = NULL;
    for (CorNode* instanceP = attrP->value.firstChildP; instanceP != NULL; instanceP = instanceP->next)
    {
      if (instanceP->type == CorObject && corTreeLookup(instanceP, "datasetId") == NULL)
      {
        defaultInstanceP = instanceP;
        break;
      }
    }

    if (defaultInstanceP == NULL)
      return NULL;

    attrP = defaultInstanceP;
  }

  if (attrP->type != CorObject)
    return NULL;

  CorNode* valueP = corTreeLookup(attrP, "value");

  if (valueP != NULL && valueP->type == CorObject)
    return corTreeClone(allocP, valueP);

  // Simplified — the attribute IS the geometry. Cheap shape check: a
  // GeoJSON geometry has a string `type` whose value is one of the
  // standard geometry kinds.
  CorNode* typeP = corTreeLookup(attrP, "type");
  if (typeP == NULL || typeP->type != CorString)
    return NULL;

  // NGSI-LD § 4.7.1 / § 4.6.1: all GeoJSON geometries are allowed
  // except GeometryCollection.
  const char* t = typeP->value.s;
  if (strcmp(t, "Point")           != 0 &&
      strcmp(t, "MultiPoint")      != 0 &&
      strcmp(t, "LineString")      != 0 &&
      strcmp(t, "MultiLineString") != 0 &&
      strcmp(t, "Polygon")         != 0 &&
      strcmp(t, "MultiPolygon")    != 0)
    return NULL;

  return corTreeClone(allocP, attrP);
}



// -----------------------------------------------------------------------------
//
// entityToFeature - wrap a single entity as a GeoJSON Feature
//
static CorNode* entityToFeature(CorNode* entityP, const char* geoPropName, KAlloc* allocP)
{
  CorNode* feature = corTreeObject(allocP, NULL);

  // "type": "Feature"
  corTreeChildAdd(feature, corTreeString(allocP, "type", "Feature"));

  // "id": entity id
  CorNode* idP = corTreeLookup(entityP, "id");
  if (idP != NULL && idP->type == CorString)
    corTreeChildAdd(feature, corTreeString(allocP, "id", idP->value.s));

  // "geometry": extracted from the selected GeoProperty
  CorNode* geometry = extractGeometry(entityP, geoPropName, allocP);
  if (geometry != NULL)
  {
    geometry->name = (char*) "geometry";
    corTreeChildAdd(feature, geometry);
  }
  else
  {
    corTreeChildAdd(feature, corTreeNull(allocP, "geometry"));
  }

  // "properties": clone the entity, remove "id" (already at Feature level)
  CorNode* properties = corTreeClone(allocP, entityP);
  properties->name = (char*) "properties";

  CorNode* propsId = corTreeLookup(properties, "id");
  if (propsId != NULL)
    corTreeChildRemove(properties, propsId);

  // The geometry GeoProperty was protected from the pick/omit/attrs projection
  // so the "geometry" field above could be built (§ 5.3.3.2). If the user's
  // projection would actually have dropped it, it must not leak into
  // "properties" (§ 5.3.3.3.1: properties honour the projection rules).
  if (corNgsild.geoJsonGeomForced)
  {
    CorNode* geoProp = corTreeLookup(properties, geoPropName);
    if (geoProp != NULL)
      corTreeChildRemove(properties, geoProp);
  }

  corTreeChildAdd(feature, properties);

  return feature;
}



// -----------------------------------------------------------------------------
//
// ldToGeoJson - transform response tree to GeoJSON
//
void ldToGeoJson(CorNode** treePP, const char* geometryProperty, KAlloc* allocP)
{
  CorNode* treeP = *treePP;
  if (treeP == NULL)
    return;

  const char* geoPropName = (geometryProperty != NULL) ? geometryProperty : "location";

  if (treeP->type == CorObject)
  {
    // Single entity -> Feature
    *treePP = entityToFeature(treeP, geoPropName, allocP);
  }
  else if (treeP->type == CorArray)
  {
    // Array of entities -> FeatureCollection
    CorNode* fc = corTreeObject(allocP, NULL);
    corTreeChildAdd(fc, corTreeString(allocP, "type", "FeatureCollection"));

    CorNode* features = corTreeArray(allocP, "features");

    for (CorNode* entityP = treeP->value.firstChildP; entityP != NULL; entityP = entityP->next)
    {
      CorNode* featureP = entityToFeature(entityP, geoPropName, allocP);
      corTreeChildAdd(features, featureP);
    }

    corTreeChildAdd(fc, features);
    *treePP = fc;
  }
}
