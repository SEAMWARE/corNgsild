//
// FILE            ldToGeoJson.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stddef.h>                                      // NULL
#include <string.h>                                      // strcmp

#include "kjson/KjNode.h"                                // KjNode
#include "kjson/kjBuilder.h"                             // kjObject, kjString, kjArray, kjChildAdd, kjNull
#include "kjson/kjLookup.h"                              // kjLookup
#include "kjson/kjClone.h"                               // kjClone

#include "swNgsild/SwNgsild.h"                           // swNgsild (geoJsonGeomForced)
#include "swNgsild/ldToGeoJson.h"                        // Own interface



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
static KjNode* extractGeometry(KjNode* entityP, const char* geoPropName, Kjson* kjsonP)
{
  KjNode* attrP = kjLookup(entityP, geoPropName);

  if (attrP == NULL)
    return NULL;

  // Multi-instance GeoProperty (§ 5.3.3.2): with no datasetId in the request the
  // attribute arrives here as an array of instances. Select the DEFAULT instance
  // (the one without a datasetId); if there is no default, the geometry is
  // undefined. A request datasetId has already collapsed the array to one object.
  if (attrP->type == KjArray)
  {
    KjNode* defaultInstanceP = NULL;
    for (KjNode* instanceP = attrP->value.firstChildP; instanceP != NULL; instanceP = instanceP->next)
    {
      if (instanceP->type == KjObject && kjLookup(instanceP, "datasetId") == NULL)
      {
        defaultInstanceP = instanceP;
        break;
      }
    }

    if (defaultInstanceP == NULL)
      return NULL;

    attrP = defaultInstanceP;
  }

  if (attrP->type != KjObject)
    return NULL;

  KjNode* valueP = kjLookup(attrP, "value");

  if (valueP != NULL && valueP->type == KjObject)
    return kjClone(kjsonP, valueP);

  // Simplified — the attribute IS the geometry. Cheap shape check: a
  // GeoJSON geometry has a string `type` whose value is one of the
  // standard geometry kinds.
  KjNode* typeP = kjLookup(attrP, "type");
  if (typeP == NULL || typeP->type != KjString)
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

  return kjClone(kjsonP, attrP);
}



// -----------------------------------------------------------------------------
//
// entityToFeature - wrap a single entity as a GeoJSON Feature
//
static KjNode* entityToFeature(KjNode* entityP, const char* geoPropName, Kjson* kjsonP)
{
  KjNode* feature = kjObject(kjsonP, NULL);

  // "type": "Feature"
  kjChildAdd(feature, kjString(kjsonP, "type", "Feature"));

  // "id": entity id
  KjNode* idP = kjLookup(entityP, "id");
  if (idP != NULL && idP->type == KjString)
    kjChildAdd(feature, kjString(kjsonP, "id", idP->value.s));

  // "geometry": extracted from the selected GeoProperty
  KjNode* geometry = extractGeometry(entityP, geoPropName, kjsonP);
  if (geometry != NULL)
  {
    geometry->name = (char*) "geometry";
    kjChildAdd(feature, geometry);
  }
  else
  {
    kjChildAdd(feature, kjNull(kjsonP, "geometry"));
  }

  // "properties": clone the entity, remove "id" (already at Feature level)
  KjNode* properties = kjClone(kjsonP, entityP);
  properties->name = (char*) "properties";

  KjNode* propsId = kjLookup(properties, "id");
  if (propsId != NULL)
    kjChildRemove(properties, propsId);

  // The geometry GeoProperty was protected from the pick/omit/attrs projection
  // so the "geometry" field above could be built (§ 5.3.3.2). If the user's
  // projection would actually have dropped it, it must not leak into
  // "properties" (§ 5.3.3.3.1: properties honour the projection rules).
  if (swNgsild.geoJsonGeomForced)
  {
    KjNode* geoProp = kjLookup(properties, geoPropName);
    if (geoProp != NULL)
      kjChildRemove(properties, geoProp);
  }

  kjChildAdd(feature, properties);

  return feature;
}



// -----------------------------------------------------------------------------
//
// ldToGeoJson - transform response tree to GeoJSON
//
void ldToGeoJson(KjNode** treePP, const char* geometryProperty, Kjson* kjsonP)
{
  KjNode* treeP = *treePP;
  if (treeP == NULL)
    return;

  const char* geoPropName = (geometryProperty != NULL) ? geometryProperty : "location";

  if (treeP->type == KjObject)
  {
    // Single entity -> Feature
    *treePP = entityToFeature(treeP, geoPropName, kjsonP);
  }
  else if (treeP->type == KjArray)
  {
    // Array of entities -> FeatureCollection
    KjNode* fc = kjObject(kjsonP, NULL);
    kjChildAdd(fc, kjString(kjsonP, "type", "FeatureCollection"));

    KjNode* features = kjArray(kjsonP, "features");

    for (KjNode* entityP = treeP->value.firstChildP; entityP != NULL; entityP = entityP->next)
    {
      KjNode* featureP = entityToFeature(entityP, geoPropName, kjsonP);
      kjChildAdd(features, featureP);
    }

    kjChildAdd(fc, features);
    *treePP = fc;
  }
}
