//
// FILE            ldSimplifyEntity.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Transform normalized entity attributes to simplified or concise format.
//
// These are OUTPUT transforms for notification payloads — the entity is
// already in API format (after ldEntityToApi + ldStripSysAttrs + compact).
// Each attribute is a JSON object with "type" and "value"/"object"/etc.
//
// Simplified (keyValues):
//   Property:          { "type": "Property", "value": X }           → X
//   Relationship:      { "type": "Relationship", "object": "uri" }  → "uri"
//   GeoProperty:       { "type": "GeoProperty", "value": {...} }    → {...}  (GeoJSON)
//   LanguageProperty:  { "type": "LanguageProperty", "languageMap": {...} } → {"languageMap": {...}}
//   VocabProperty:     { "type": "VocabProperty", "vocab": "X" }   → "X"
//   JsonProperty:      { "type": "JsonProperty", "json": {...} }   → {"json": {...}}
//   ListProperty:      { "type": "ListProperty", "valueList": [...] } → [...]
//   ListRelationship:  { "type": "ListRelationship", "objectList": [...] } → [...]
//
// Concise:
//   Property (no sub-attrs):  → X  (same as simplified)
//   Property (with sub-attrs): drop "type", keep "value" + sub-attrs
//   Relationship:       always { "object": "uri", ... } — drop "type" only
//   GeoProperty:        stays normalized (value is JSON object with "type" field)
//   LanguageProperty:   { "languageMap": {...}, ... } — drop "type"
//   VocabProperty:      { "vocab": "X", ... } — drop "type"
//   JsonProperty:       { "json": {...}, ... } — drop "type"
//   ListProperty (no sub-attrs): → [...]  (same as simplified)
//   ListProperty (with sub-attrs): drop "type", keep "valueList" + sub-attrs
//   ListRelationship:   { "objectList": [...], ... } — drop "type"
//
#include <stdbool.h>                                   // bool
#include <string.h>                                    // strcmp

#include "kjson/KjNode.h"                              // KjNode
#include "kjson/kjLookup.h"                            // kjLookup
#include "kjson/kjBuilder.h"                           // kjChildRemove, kjObject, kjChildAdd

#include "swNgsild/ldSimplifyEntity.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// isEntityKeyword - id, type, scope are entity-level fields, not attributes
//
static bool isEntityKeyword(const char* name)
{
  if (strcmp(name, "id")    == 0) return true;
  if (strcmp(name, "type")  == 0) return true;
  if (strcmp(name, "scope") == 0) return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// attrSubAttrCount - count children that are not "type", "value", "object", etc.
//
// Returns the number of "extra" fields beyond the mandatory type + value/object/etc.
//
static int attrSubAttrCount(KjNode* attrP, const char* typeStr)
{
  int count = 0;

  for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, "type") == 0) continue;

    // Skip the primary value field depending on attr type
    if (strcmp(typeStr, "Property") == 0      && strcmp(childP->name, "value") == 0)       continue;
    if (strcmp(typeStr, "Relationship") == 0  && strcmp(childP->name, "object") == 0)      continue;
    if (strcmp(typeStr, "GeoProperty") == 0   && strcmp(childP->name, "value") == 0)       continue;
    if (strcmp(typeStr, "LanguageProperty") == 0 && strcmp(childP->name, "languageMap") == 0) continue;
    if (strcmp(typeStr, "VocabProperty") == 0 && strcmp(childP->name, "vocab") == 0)       continue;
    if (strcmp(typeStr, "JsonProperty") == 0  && strcmp(childP->name, "json") == 0)        continue;
    if (strcmp(typeStr, "ListProperty") == 0  && strcmp(childP->name, "valueList") == 0)   continue;
    if (strcmp(typeStr, "ListRelationship") == 0 && strcmp(childP->name, "objectList") == 0) continue;

    count++;
  }

  return count;
}



// -----------------------------------------------------------------------------
//
// primaryValueKey - the simplified-value key name for a given attribute type
//
static const char* primaryValueKey(const char* typeStr)
{
  if (strcmp(typeStr, "Property")         == 0) return "value";
  if (strcmp(typeStr, "GeoProperty")      == 0) return "value";
  if (strcmp(typeStr, "Relationship")     == 0) return "object";
  if (strcmp(typeStr, "LanguageProperty") == 0) return "languageMap";
  if (strcmp(typeStr, "VocabProperty")    == 0) return "vocab";
  if (strcmp(typeStr, "JsonProperty")     == 0) return "json";
  if (strcmp(typeStr, "ListProperty")     == 0) return "valueList";
  if (strcmp(typeStr, "ListRelationship") == 0) return "objectList";
  return NULL;
}



// -----------------------------------------------------------------------------
//
// ldSimplifyEntity -
//
// Per spec § 4.5.4:
//   - Single-instance attribute  → just the value
//   - Multi-instance (array)     → { "dataset": { <dsKey>: <simplifiedValue>, ... } }
//     where dsKey is the datasetId or "@none" for the default instance.
//
void ldSimplifyEntity(KjNode* entityP)
{
  if (entityP == NULL || entityP->type != KjObject)
    return;

  KjNode* childP = entityP->value.firstChildP;

  while (childP != NULL)
  {
    KjNode* nextP = childP->next;

    if (childP->name == NULL || isEntityKeyword(childP->name))
    {
      childP = nextP;
      continue;
    }

    //
    // Multi-instance: KjArray of attribute objects (one per datasetId).
    // Build a `dataset` map keyed by datasetId / "@none".
    //
    if (childP->type == KjArray)
    {
      KjNode* datasetMap = kjObject(NULL, "dataset");

      for (KjNode* instP = childP->value.firstChildP; instP != NULL; instP = instP->next)
      {
        if (instP->type != KjObject)
          continue;

        KjNode* typeP = kjLookup(instP, "type");
        if (typeP == NULL || typeP->type != KjString)
          continue;

        const char* valKey = primaryValueKey(typeP->value.s);
        if (valKey == NULL)
          continue;

        KjNode* valP = kjLookup(instP, valKey);
        if (valP == NULL)
          continue;

        KjNode* dsP  = kjLookup(instP, "datasetId");
        const char* dsKey = (dsP != NULL && dsP->type == KjString) ? dsP->value.s : "@none";

        // Detach the value node and rename it to the dsKey.
        kjChildRemove(instP, valP);
        valP->name = (char*) dsKey;
        kjChildAdd(datasetMap, valP);
      }

      // Replace the array with an object  { "dataset": <map> }.
      childP->type            = KjObject;
      childP->value.firstChildP = NULL;
      childP->lastChild       = NULL;
      kjChildAdd(childP, datasetMap);

      childP = nextP;
      continue;
    }

    if (childP->type != KjObject)
    {
      childP = nextP;
      continue;
    }

    KjNode* typeP = kjLookup(childP, "type");
    if (typeP == NULL || typeP->type != KjString)
    {
      childP = nextP;
      continue;
    }

    const char* valKey    = primaryValueKey(typeP->value.s);
    KjNode*     valueNode = (valKey != NULL) ? kjLookup(childP, valKey) : NULL;

    if (valueNode != NULL)
    {
      // Replace the attribute object with just the value
      // Copy the value node's content into the attribute node
      childP->type            = valueNode->type;
      childP->value           = valueNode->value;
    }

    childP = nextP;
  }
}



// -----------------------------------------------------------------------------
//
// ldConciseEntity -
//
void ldConciseEntity(KjNode* entityP)
{
  if (entityP == NULL || entityP->type != KjObject)
    return;

  KjNode* childP = entityP->value.firstChildP;

  while (childP != NULL)
  {
    KjNode* nextP = childP->next;

    if (childP->name == NULL || isEntityKeyword(childP->name) || childP->type != KjObject)
    {
      childP = nextP;
      continue;
    }

    KjNode* typeP = kjLookup(childP, "type");
    if (typeP == NULL || typeP->type != KjString)
    {
      childP = nextP;
      continue;
    }

    const char* typeStr = typeP->value.s;

    //
    // GeoProperty: stays normalized in concise (value is JSON object with "type" field)
    //
    if (strcmp(typeStr, "GeoProperty") == 0)
    {
      childP = nextP;
      continue;
    }

    int subAttrs = attrSubAttrCount(childP, typeStr);

    //
    // Property / ListProperty without sub-attrs: collapse to just the value
    //
    if (subAttrs == 0 && (strcmp(typeStr, "Property") == 0 || strcmp(typeStr, "ListProperty") == 0))
    {
      const char* valField = (strcmp(typeStr, "Property") == 0) ? "value" : "valueList";
      KjNode* valueNode = kjLookup(childP, valField);

      if (valueNode != NULL)
      {
        childP->type  = valueNode->type;
        childP->value = valueNode->value;
      }

      childP = nextP;
      continue;
    }

    //
    // All other cases: drop "type" field, keep everything else
    // This covers:
    //   - Property/ListProperty WITH sub-attrs (keep value/valueList + sub-attrs)
    //   - Relationship (keep object + optional sub-attrs)
    //   - LanguageProperty (keep languageMap + optional sub-attrs)
    //   - VocabProperty (keep vocab + optional sub-attrs)
    //   - JsonProperty (keep json + optional sub-attrs)
    //   - ListRelationship (keep objectList + optional sub-attrs)
    //
    kjChildRemove(childP, typeP);

    childP = nextP;
  }
}
