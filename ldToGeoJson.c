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

#include "swNgsild/ldToGeoJson.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// extractGeometry - extract GeoJSON geometry from an entity's GeoProperty
//
// In API format (after compaction):
//   "location": { "type": "GeoProperty", "value": { "type": "Point", "coordinates": [...] } }
//
static KjNode* extractGeometry(KjNode* entityP, const char* geoPropName, Kjson* kjsonP)
{
  KjNode* attrP = kjLookup(entityP, geoPropName);

  if (attrP == NULL || attrP->type != KjObject)
    return NULL;

  KjNode* valueP = kjLookup(attrP, "value");

  if (valueP != NULL && valueP->type == KjObject)
    return kjClone(kjsonP, valueP);

  return NULL;
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
