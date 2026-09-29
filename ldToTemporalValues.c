//
// FILE            ldToTemporalValues.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// § 4.5.8 — simplified temporal representation. See header for the shape.
//
#include <stddef.h>                                      // NULL
#include <string.h>                                      // strcmp

#include "corAlloc/CorAlloc.h"                          // CorAlloc
#include "corTree/CorNode.h"                             // CorNode
#include "corTree/corTreeBuilder.h"                      // corTreeObject, corTreeArray, corTreeString
#include "corTree/corTreeLookup.h"                       // corTreeLookup
#include "corTree/corTreeClone.h"                        // corTreeClone

#include "corJsonld/corLdCompact.h"                      // corLdCompact
#include "corJsonld/corLdInit.h"                         // corLdCoreContext

#include "corNgsild/CorNgsild.h"                         // corNgsild (for response @context)
#include "corNgsild/ldIsEntityKeyword.h"                 // ldIsEntityKeyword
#include "corNgsild/ldTermId.h"                           // ldTermId, CorTerm*
#include "corNgsild/ldToTemporalValues.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// vocabCompactInPlace - compact a VocabProperty's `vocab` value(s) in place.
//
// The temporal store keeps vocab IRIs in expanded form (e.g.
// "https://uri.etsi.org/ngsi-ld/default-context/monument"). On the
// temporalValues render path, corLdCompactTree only walks Object/Object-Array
// shapes — the per-instance pair `[ {vocab: "..."}, ts ]` lives one Array
// layer below where the generic walker reaches, so the inner string never
// gets compacted to its short form ("monument"). Compact here, inline,
// using the response @context (request context, falling back to core).
//
static void vocabCompactInPlace(CorNode* valP)
{
  if (valP == NULL)
    return;

  CorLdContext* ctxP = (corNgsild.contextP != NULL) ? corNgsild.contextP : corLdCoreContext();
  if (ctxP == NULL)
    return;

  if (valP->type == CorString)
  {
    const char* cv = corLdCompact(ctxP, valP->value.s);
    if (cv != NULL)
      valP->value.s = (char*) cv;
  }
  else if (valP->type == CorArray)
  {
    for (CorNode* itemP = valP->value.head; itemP != NULL; itemP = itemP->next)
    {
      if (itemP->type != CorString)
        continue;
      const char* cv = corLdCompact(ctxP, itemP->value.s);
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
static void addPair(CorNode*     valuesArray,
                    CorNode*     instP,
                    const char*  firstKey,
                    bool         wrapped,
                    const char*  timeProp,
                    CorAlloc*    allocP)
{
  CorNode* valP = corTreeLookup(instP, firstKey);
  CorNode* tsP = corTreeLookup(instP, timeProp);
  CorNode* pair = corTreeArray(allocP, NULL);

  if (valP != NULL)
  {
    CorNode* clone = corTreeClone(allocP, valP);
    if (wrapped)
    {
      // JsonProperty / VocabProperty: pair is [{json/vocab: value}, ts].
      // VocabProperty's `vocab` is @type:@vocab — IRIs in the store have
      // to come out compacted on the wire.
      if (strcmp(firstKey, "vocab") == 0)
        vocabCompactInPlace(clone);
      ldNodeRename(clone, (char*) firstKey);
      CorNode* wrapper = corTreeObject(allocP, NULL);
      corTreeChildAdd(wrapper, clone);
      corTreeChildAdd(pair, wrapper);
    }
    else
    {
      ldNodeRename(clone, NULL);
      corTreeChildAdd(pair, clone);
    }
  }
  else
  {
    corTreeChildAdd(pair, corTreeNull(allocP, NULL));
  }

  if (tsP != NULL && tsP->type == CorString)
    corTreeChildAdd(pair, corTreeString(allocP, NULL, tsP->value.s));
  else
    corTreeChildAdd(pair, corTreeNull(allocP, NULL));

  corTreeChildAdd(valuesArray, pair);
}



// -----------------------------------------------------------------------------
//
// transformAttr - replace a CorArray-of-instances attr with the simplified
//                 simplified-temporal shape. The attr's instances are grouped
//                 by datasetId (§ 4.5.5: Attribute instances with distinct
//                 datasetId are independent series). When there is exactly
//                 one group (the default — i.e. all instances share the
//                 same or no datasetId), the attr collapses to a single
//                 object { type, valuesKey: [...] }. When there are
//                 multiple groups, the attr becomes an ARRAY of such
//                 objects, each carrying its own "datasetId" member.
//
static void transformAttr(CorNode* attrP, const char* timeProp, CorAlloc* allocP, CorAlloc* faP)
{
  if (attrP == NULL || attrP->type != CorArray)
    return;

  // Inspect the first instance to learn the attr type and key shape.
  CorNode* firstP = attrP->value.head;
  if (firstP == NULL || firstP->type != CorObject)
  {
    // Empty or malformed — collapse to a minimal Property/values:[] object.
    attrP->type = CorObject;
    attrP->value.head = NULL;
    attrP->value.tail        = NULL;
    corTreeChildAdd(attrP, corTreeString(allocP, "type", "Property"));
    corTreeChildAdd(attrP, corTreeArray(allocP, "values"));
    return;
  }

  CorNode* typeP = corTreeLookup(firstP, "type");
  const char* attrType  = (typeP != NULL && typeP->type == CorString) ? typeP->value.s : "Property";
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
  CorNode*    valuesArrV[MAX_DATASETS];
  int         dsCount = 0;

  for (CorNode* instP = firstP; instP != NULL; instP = instP->next)
  {
    if (instP->type != CorObject)
      continue;

    CorNode* dsP = corTreeLookup(instP, "datasetId");
    const char* ds = (dsP != NULL && dsP->type == CorString) ? dsP->value.s : NULL;

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
      valuesArrV[slot]  = corTreeArray(allocP, valuesKey);
    }

    addPair(valuesArrV[slot], instP, firstKey, wrapped, timeProp, allocP);
  }

  if (dsCount == 0)
  {
    // No instances at all (every one was malformed). Render an empty
    // single-bucket shape for consistency with the no-instances guard
    // above.
    attrP->type              = CorObject;
    attrP->value.head = NULL;
    attrP->value.tail        = NULL;
    corTreeChildAdd(attrP, corTreeString(allocP, "type", attrType));
    corTreeChildAdd(attrP, corTreeArray(allocP, valuesKey));
    return;
  }

  // Determine output shape: single object when one bucket survives,
  // array of per-datasetId objects when more than one.
  if (dsCount == 1)
  {
    attrP->type              = CorObject;
    attrP->value.head = NULL;
    attrP->value.tail        = NULL;
    corTreeChildAdd(attrP, corTreeString(allocP, "type", attrType));
    if (dsId[0] != NULL)
      corTreeChildAdd(attrP, corTreeString(allocP, "datasetId", (char*) dsId[0]));
    corTreeChildAdd(attrP, valuesArrV[0]);
    return;
  }

  // Multi-datasetId: keep attrP as a CorArray, replace its children.
  attrP->value.head = NULL;
  attrP->value.tail        = NULL;
  for (int i = 0; i < dsCount; i++)
  {
    CorNode* obj = corTreeObject(allocP, NULL);
    corTreeChildAdd(obj, corTreeString(allocP, "type", attrType));
    if (dsId[i] != NULL)
      corTreeChildAdd(obj, corTreeString(allocP, "datasetId", (char*) dsId[i]));
    corTreeChildAdd(obj, valuesArrV[i]);
    corTreeChildAdd(attrP, obj);
  }
}



// -----------------------------------------------------------------------------
//
// transformEntity - apply transformAttr to every attribute child of one entity
//
static void transformEntity(CorNode* entityP, const char* timeProp, CorAlloc* allocP, CorAlloc* faP)
{
  if (entityP == NULL || entityP->type != CorObject)
    return;

  for (CorNode* childP = entityP->value.head; childP != NULL; childP = childP->next)
  {
    if (childP->name == NULL)
      continue;
    // § 5.3.2.5: the Scope's temporal evolution is represented as a Property,
    // so it gets the same pair-compression as any attr in simplified-temporal
    // mode (see TS 104-176 § A.3.4.3 for the wire example). Every other
    // entity keyword stays untouched.
    if (ldIsEntityMember(childP) && ldTermId(childP) != CorTermScope)
      continue;
    if (childP->type != CorArray)
      continue;
    transformAttr(childP, timeProp, allocP, faP);
  }
}



// -----------------------------------------------------------------------------
//
// ldToTemporalValues -
//
void ldToTemporalValues(CorNode* treeP, const char* timeProp, CorAlloc* allocP, CorAlloc* faP)
{
  if (treeP == NULL)
    return;

  if (timeProp == NULL || timeProp[0] == 0)
    timeProp = "observedAt";

  if (treeP->type == CorArray)
  {
    for (CorNode* itemP = treeP->value.head; itemP != NULL; itemP = itemP->next)
      transformEntity(itemP, timeProp, allocP, faP);
  }
  else
  {
    transformEntity(treeP, timeProp, allocP, faP);
  }
}
