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

#include "swJsonld/swldCompact.h"                        // swldCompact
#include "swJsonld/swldInit.h"                           // swldCoreContext

#include "swNgsild/SwNgsild.h"                           // swNgsild (for response @context)
#include "swNgsild/ldIsEntityKeyword.h"                  // ldIsEntityKeyword
#include "swNgsild/ldToTemporalValues.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// vocabCompactInPlace - compact a VocabProperty's `vocab` value(s) in place.
//
// The temporal store keeps vocab IRIs in expanded form (e.g.
// "https://uri.etsi.org/ngsi-ld/default-context/monument"). On the
// temporalValues render path, swldCompactTree only walks Object/Object-Array
// shapes — the per-instance pair `[ {vocab: "..."}, ts ]` lives one Array
// layer below where the generic walker reaches, so the inner string never
// gets compacted to its short form ("monument"). Compact here, inline,
// using the response @context (request context, falling back to core).
//
static void vocabCompactInPlace(KjNode* valP)
{
  if (valP == NULL)
    return;

  SwldContext* ctxP = (swNgsild.contextP != NULL) ? swNgsild.contextP : swldCoreContext();
  if (ctxP == NULL)
    return;

  if (valP->type == KjString)
  {
    const char* cv = swldCompact(ctxP, valP->value.s);
    if (cv != NULL)
      valP->value.s = (char*) cv;
  }
  else if (valP->type == KjArray)
  {
    for (KjNode* itemP = valP->value.firstChildP; itemP != NULL; itemP = itemP->next)
    {
      if (itemP->type != KjString)
        continue;
      const char* cv = swldCompact(ctxP, itemP->value.s);
      if (cv != NULL)
        itemP->value.s = (char*) cv;
    }
  }
}



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
// the wrapped {key: value} object instead of a bare value (§ 4.5.9 —
// see EXAMPLE 2 for LanguageProperty {"languageMap": {...}}, EXAMPLE 4
// for JsonProperty {"json": ...}, EXAMPLE 5 for VocabProperty
// {"vocab": ...}).
//
static bool firstElementWrapped(const char* attrType)
{
  if (attrType == NULL) return false;
  if (strcmp(attrType, "LanguageProperty") == 0) return true;
  if (strcmp(attrType, "JsonProperty")     == 0) return true;
  if (strcmp(attrType, "VocabProperty")    == 0) return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// addPair - render one instance's [value, timestamp] pair into valuesArray.
//
static void addPair(KjNode*      valuesArray,
                    KjNode*      instP,
                    const char*  firstKey,
                    bool         wrapped,
                    const char*  timeProp,
                    Kjson*       kjsonP)
{
  KjNode* valP = kjLookup(instP, firstKey);
  KjNode* tsP  = kjLookup(instP, timeProp);
  KjNode* pair = kjArray(kjsonP, NULL);

  if (valP != NULL)
  {
    KjNode* clone = kjClone(kjsonP, valP);
    if (wrapped)
    {
      // JsonProperty / VocabProperty: pair is [{json/vocab: value}, ts].
      // VocabProperty's `vocab` is @type:@vocab — IRIs in the store have
      // to come out compacted on the wire.
      if (strcmp(firstKey, "vocab") == 0)
        vocabCompactInPlace(clone);
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



// -----------------------------------------------------------------------------
//
// transformAttr - replace a KjArray-of-instances attr with the simplified
//                 simplified-temporal shape. The attr's instances are grouped
//                 by datasetId (§ 4.5.5: Attribute instances with distinct
//                 datasetId are independent series). When there is exactly
//                 one group (the default — i.e. all instances share the
//                 same or no datasetId), the attr collapses to a single
//                 object { type, valuesKey: [...] }. When there are
//                 multiple groups, the attr becomes an ARRAY of such
//                 objects, each carrying its own "datasetId" member.
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

  // Group instances by datasetId, lazily — buckets are created only as
  // instances are seen, so an upstream datasetId filter that excludes
  // the default-dataset rows doesn't leave an empty default bucket
  // dangling in the response (§ 4.5.5).
  // Cap at 32 distinct series — typical entities have <10.
  enum { MAX_DATASETS = 32 };
  const char* dsId[MAX_DATASETS];        // NULL = default-dataset bucket
  KjNode*     valuesArrV[MAX_DATASETS];
  int         dsCount = 0;

  for (KjNode* instP = firstP; instP != NULL; instP = instP->next)
  {
    if (instP->type != KjObject)
      continue;

    KjNode* dsP = kjLookup(instP, "datasetId");
    const char* ds = (dsP != NULL && dsP->type == KjString) ? dsP->value.s : NULL;

    // Find or create the bucket for this datasetId.
    int slot = -1;
    for (int i = 0; i < dsCount; i++)
    {
      const char* a = dsId[i];
      if (a == NULL && ds == NULL)            { slot = i; break; }
      if (a != NULL && ds != NULL && strcmp(a, ds) == 0) { slot = i; break; }
    }
    if (slot < 0)
    {
      if (dsCount >= MAX_DATASETS) continue;   // bail safely on pathological input
      slot              = dsCount++;
      dsId[slot]        = ds;
      valuesArrV[slot]  = kjArray(kjsonP, valuesKey);
    }

    addPair(valuesArrV[slot], instP, firstKey, wrapped, timeProp, kjsonP);
  }

  if (dsCount == 0)
  {
    // No instances at all (every one was malformed). Render an empty
    // single-bucket shape for consistency with the no-instances guard
    // above.
    attrP->type              = KjObject;
    attrP->value.firstChildP = NULL;
    attrP->lastChild         = NULL;
    kjChildAdd(attrP, kjString(kjsonP, "type", attrType));
    kjChildAdd(attrP, kjArray(kjsonP, valuesKey));
    return;
  }

  // Determine output shape: single object when one bucket survives,
  // array of per-datasetId objects when more than one.
  if (dsCount == 1)
  {
    attrP->type              = KjObject;
    attrP->value.firstChildP = NULL;
    attrP->lastChild         = NULL;
    kjChildAdd(attrP, kjString(kjsonP, "type", attrType));
    if (dsId[0] != NULL)
      kjChildAdd(attrP, kjString(kjsonP, "datasetId", (char*) dsId[0]));
    kjChildAdd(attrP, valuesArrV[0]);
    return;
  }

  // Multi-datasetId: keep attrP as a KjArray, replace its children.
  attrP->value.firstChildP = NULL;
  attrP->lastChild         = NULL;
  for (int i = 0; i < dsCount; i++)
  {
    KjNode* obj = kjObject(kjsonP, NULL);
    kjChildAdd(obj, kjString(kjsonP, "type", attrType));
    if (dsId[i] != NULL)
      kjChildAdd(obj, kjString(kjsonP, "datasetId", (char*) dsId[i]));
    kjChildAdd(obj, valuesArrV[i]);
    kjChildAdd(attrP, obj);
  }
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
