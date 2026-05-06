//
// FILE            ldToTemporalValues.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// § 4.5.8 — simplified temporal representation. See header for the shape.
//
#include <stddef.h>                                      // NULL
#include <string.h>                                      // strcmp

#include "kalloc/KAlloc.h"                              // KAlloc
#include "kjson/kjson.h"                                // Kjson
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjBuilder.h"                            // kjObject, kjArray, kjString
#include "kjson/kjLookup.h"                             // kjLookup
#include "kjson/kjClone.h"                              // kjClone

#include "swNgsild/ldIsEntityKeyword.h"                  // ldIsEntityKeyword
#include "swNgsild/ldToTemporalValues.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// valuesKeyForType - the temporal-values container key for each attr type.
//
//   § 4.5.7  Property         → "values"
//   § 4.5.8  GeoProperty       → "values"
//   § 4.5.7  ListProperty      → "valueLists"
//   § 4.5.7  JsonProperty      → "jsons"
//   § 4.5.18 LanguageProperty  → "languageMaps"
//   § 4.5.x  VocabProperty     → "vocabs"
//   § 4.5.7  Relationship      → "objects"
//   § 4.5.7  ListRelationship  → "objectLists"
//
static const char* valuesKeyForType(const char* attrType)
{
  if (attrType == NULL)                            return "values";
  if (strcmp(attrType, "Relationship")     == 0)   return "objects";
  if (strcmp(attrType, "ListRelationship") == 0)   return "objectLists";
  if (strcmp(attrType, "LanguageProperty") == 0)   return "languageMaps";
  if (strcmp(attrType, "ListProperty")     == 0)   return "valueLists";
  if (strcmp(attrType, "JsonProperty")     == 0)   return "jsons";
  if (strcmp(attrType, "VocabProperty")    == 0)   return "vocabs";
  return "values";
}



// -----------------------------------------------------------------------------
//
// firstElementKey - the key under each instance carrying the value
// (matches storage shape, NOT necessarily the wire shape — for the
// wrapped types JsonProperty / VocabProperty, the wire pair includes
// the {key: value} object, but the instance still stores just the
// value under that key).
//
//   Property / GeoProperty             → "value"
//   ListProperty                       → "valueList"
//   JsonProperty                       → "json"
//   VocabProperty                      → "vocab"
//   LanguageProperty                   → "languageMap"
//   Relationship                       → "object"
//   ListRelationship                   → "objectList"
//
static const char* firstElementKey(const char* attrType)
{
  if (attrType == NULL)                            return "value";
  if (strcmp(attrType, "Relationship")     == 0)   return "object";
  if (strcmp(attrType, "ListRelationship") == 0)   return "objectList";
  if (strcmp(attrType, "LanguageProperty") == 0)   return "languageMap";
  if (strcmp(attrType, "ListProperty")     == 0)   return "valueList";
  if (strcmp(attrType, "JsonProperty")     == 0)   return "json";
  if (strcmp(attrType, "VocabProperty")    == 0)   return "vocab";
  return "value";
}



// -----------------------------------------------------------------------------
//
// firstElementWrapped - true when the wire pair's first element must be
// the wrapped {key: value} object instead of a bare value (§ 4.5.7 for
// JsonProperty, § 4.5.x for VocabProperty).
//
static bool firstElementWrapped(const char* attrType)
{
  if (attrType == NULL) return false;
  if (strcmp(attrType, "JsonProperty")  == 0) return true;
  if (strcmp(attrType, "VocabProperty") == 0) return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// transformAttr - replace a KjArray-of-instances attr with the simplified
//                 "{ type, values: [[value, ts], ...] }" object.
//
static void transformAttr(KjNode* attrP, const char* timeProp, Kjson* kjsonP, KAlloc* faP)
{
  if (attrP == NULL || attrP->type != KjArray)
    return;

  // Inspect the first instance to learn the attr type and key shape.
  KjNode* firstP = attrP->value.firstChildP;
  if (firstP == NULL || firstP->type != KjObject)
  {
    // Empty or malformed — collapse to a minimal Property/values:[] object.
    attrP->type = KjObject;
    attrP->value.firstChildP = NULL;
    attrP->lastChild         = NULL;
    kjChildAdd(attrP, kjString(kjsonP, "type", "Property"));
    kjChildAdd(attrP, kjArray(kjsonP, "values"));
    return;
  }

  KjNode* typeP = kjLookup(firstP, "type");
  const char* attrType  = (typeP != NULL && typeP->type == KjString) ? typeP->value.s : "Property";
  const char* valuesKey = valuesKeyForType(attrType);
  const char* firstKey  = firstElementKey(attrType);
  bool        wrapped   = firstElementWrapped(attrType);

  // Build the new "values" array by walking each instance.
  KjNode* valuesArray = kjArray(kjsonP, valuesKey);

  for (KjNode* instP = firstP; instP != NULL; instP = instP->next)
  {
    if (instP->type != KjObject)
      continue;

    KjNode* valP = kjLookup(instP, firstKey);
    KjNode* tsP  = kjLookup(instP, timeProp);

    KjNode* pair = kjArray(kjsonP, NULL);

    if (valP != NULL)
    {
      KjNode* clone = kjClone(kjsonP, valP);
      if (wrapped)
      {
        // JsonProperty / VocabProperty: pair is [{json/vocab: value}, ts].
        clone->name = (char*) firstKey;
        KjNode* wrapper = kjObject(kjsonP, NULL);
        kjChildAdd(wrapper, clone);
        kjChildAdd(pair, wrapper);
      }
      else
      {
        clone->name = NULL;
        kjChildAdd(pair, clone);
      }
    }
    else
    {
      kjChildAdd(pair, kjNull(kjsonP, NULL));
    }

    if (tsP != NULL && tsP->type == KjString)
      kjChildAdd(pair, kjString(kjsonP, NULL, tsP->value.s));
    else
      kjChildAdd(pair, kjNull(kjsonP, NULL));

    kjChildAdd(valuesArray, pair);
  }

  // Replace the array contents with { type, values: [...] }.
  attrP->type              = KjObject;
  attrP->value.firstChildP = NULL;
  attrP->lastChild         = NULL;
  kjChildAdd(attrP, kjString(kjsonP, "type", attrType));
  kjChildAdd(attrP, valuesArray);
}



// -----------------------------------------------------------------------------
//
// transformEntity - apply transformAttr to every attribute child of one entity
//
static void transformEntity(KjNode* entityP, const char* timeProp, Kjson* kjsonP, KAlloc* faP)
{
  if (entityP == NULL || entityP->type != KjObject)
    return;

  for (KjNode* childP = entityP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (childP->name == NULL || ldIsEntityKeyword(childP->name))
      continue;
    if (childP->type != KjArray)
      continue;
    transformAttr(childP, timeProp, kjsonP, faP);
  }
}



// -----------------------------------------------------------------------------
//
// ldToTemporalValues -
//
void ldToTemporalValues(KjNode* treeP, const char* timeProp, Kjson* kjsonP, KAlloc* faP)
{
  if (treeP == NULL)
    return;

  if (timeProp == NULL || timeProp[0] == 0)
    timeProp = "observedAt";

  if (treeP->type == KjArray)
  {
    for (KjNode* itemP = treeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
      transformEntity(itemP, timeProp, kjsonP, faP);
  }
  else
  {
    transformEntity(treeP, timeProp, kjsonP, faP);
  }
}
