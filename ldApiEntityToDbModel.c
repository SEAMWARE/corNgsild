//
// FILE            ldApiEntityToDbModel.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#define _GNU_SOURCE
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strcmp, memset

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjBuilder.h"                             // kjObject
#include "kjson/kjChildReplace.h"                       // kjChildReplace
#include "kjson/kjLookup.h"                             // kjLookup
#include "corRest/corRest.h"                             // corRest

#include "corJsonld/corLdExpand.h"                          // KJF_ATTR_TERM
#include "corNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "corNgsild/LdAttrType.h"                         // LdAttrType, LdAttrGeoProperty
#include "corNgsild/ldAttrTypeDetect.h"                   // ldAttrTypeDetect
#include "corNgsild/ldIsEntityKeyword.h"                   // ldIsEntityKeyword
#include "corNgsild/ldCheckDateTime.h"                    // ldIsoToNanoseconds
#include "corNgsild/ldApiEntityToDbModel.h"               // Own interface



// -----------------------------------------------------------------------------
//
// ldGeoValueUnexpand - convert expanded GeoJSON IRIs back to short form
//
// After JSON-LD expansion, GeoJSON looks like:
//   { "type": "https://purl.org/geojson/vocab#Point",
//     "https://purl.org/geojson/vocab#coordinates": [-3.703, 40.417] }
//
// MongoDB 2dsphere indexes require standard GeoJSON field names,
// so we un-expand to:
//   { "type": "Point", "coordinates": [-3.703, 40.417] }
//
// This modifies the tree in-place by repointing name/value strings.
//
static void ldGeoValueUnexpand(KjNode* geoValueP)
{
  if (geoValueP == NULL || geoValueP->type != KjObject)
    return;

  for (KjNode* childP = geoValueP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    // Un-expand key names: "https://purl.org/geojson/vocab#coordinates" -> "coordinates"
    if (strncmp(childP->name, LD_VOCAB_GEOJSON_PREFIX, LD_VOCAB_GEOJSON_PREFIX_LEN) == 0)
      childP->name = childP->name + LD_VOCAB_GEOJSON_PREFIX_LEN;

    // Un-expand the "type" value: "https://purl.org/geojson/vocab#Point" -> "Point"
    if (strcmp(childP->name, "type") == 0 && childP->type == KjString)
    {
      if (strncmp(childP->value.s, LD_VOCAB_GEOJSON_PREFIX, LD_VOCAB_GEOJSON_PREFIX_LEN) == 0)
        childP->value.s = childP->value.s + LD_VOCAB_GEOJSON_PREFIX_LEN;
    }

    // Recurse into nested objects (e.g. GeometryCollection members)
    if (childP->type == KjObject)
      ldGeoValueUnexpand(childP);

    // Recurse into arrays of objects (e.g. GeometryCollection "geometries" array)
    if (childP->type == KjArray)
    {
      for (KjNode* elemP = childP->value.firstChildP; elemP != NULL; elemP = elemP->next)
      {
        if (elemP->type == KjObject)
          ldGeoValueUnexpand(elemP);
      }
    }
  }
}



// -----------------------------------------------------------------------------
//
// expandedValueKeys - all expanded IRI value keys that should be normalized to "value"
//
static const char* expandedValueKeys[] =
{
  LD_VOCAB_HAS_VALUE, LD_VOCAB_HAS_OBJECT, LD_VOCAB_HAS_LANGUAGE_MAP,
  LD_VOCAB_HAS_VOCAB, LD_VOCAB_HAS_VALUE_LIST, LD_VOCAB_HAS_OBJECT_LIST,
  LD_VOCAB_HAS_JSON, NULL
};



// -----------------------------------------------------------------------------
// isoToNanoseconds is provided by ldCheckDateTime (ldIsoToNanoseconds) — the
// single ISO 8601 → epoch-nanoseconds converter (handles fractional seconds and
// the timezone offset). See ldCheckDateTime.h.
#define isoToNanoseconds(iso) ((long long) ldIsoToNanoseconds(iso))



// -----------------------------------------------------------------------------
//
// temporalPropertiesToNanoseconds - convert observedAt string children to KjInt nanoseconds
//
static void temporalPropertiesToNanoseconds(KjNode* attrP)
{
  for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    bool isTemporal = (strcmp(childP->name, LD_VOCAB_OBSERVED_AT) == 0 ||
                       strcmp(childP->name, LD_VOCAB_EXPIRES_AT)  == 0);
    if (!isTemporal)
      continue;

    if (childP->type == KjString)
    {
      // NGSI-LD Null marker: leave the bare string so a merge/update apply
      // removes the temporal sub-attribute (§ 5.4.1) — converting it here would
      // store epoch 0 instead. (A Create/Replace with a null observedAt is
      // already rejected earlier by ldCheckAttribute.)
      if (strcmp(childP->value.s, LD_VOCAB_NGSILD_NULL) == 0)
        continue;
      childP->value.i = isoToNanoseconds(childP->value.s);
      childP->type = KjInt;
    }
    else if (childP->type == KjObject)
    {
      // JSON-LD expanded DateTime: {"@value": "2026-...", "@type": "DateTime"}
      for (KjNode* m = childP->value.firstChildP; m != NULL; m = m->next)
      {
        if (strcmp(m->name, "@value") == 0 && m->type == KjString)
        {
          childP->value.i = isoToNanoseconds(m->value.s);
          childP->type = KjInt;
          break;
        }
      }
    }
  }
}



// -----------------------------------------------------------------------------
//
// normalizeValueKey - rename any HAS_* value key to "value" in an attribute instance
//
static void normalizeValueKey(KjNode* attrP)
{
  if (attrP->type != KjObject)
    return;

  for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
    for (const char** vk = expandedValueKeys; *vk != NULL; vk++)
      if (strcmp(childP->name, *vk) == 0) { childP->name = "value"; return; }
}



// -----------------------------------------------------------------------------
//
// timestampSet - add createdAt/modifiedAt to an KjObject node
//
static void timestampSet(KjNode* objP, uint64_t createdAt, uint64_t modifiedAt, KAlloc* faP)
{
  kjChildAdd(objP, kjInteger(corRest.kjsonP, LD_VOCAB_CREATED_AT,  (long long) createdAt));
  kjChildAdd(objP, kjInteger(corRest.kjsonP, LD_VOCAB_MODIFIED_AT, (long long) modifiedAt));
}



// -----------------------------------------------------------------------------
//
// extractDatasetId - find and remove datasetId from an attribute instance
//
// Returns the datasetId value string, or "@none" if not present.
//
static const char* extractDatasetId(KjNode* attrP)
{
  KjNode* dsP = kjLookup(attrP, LD_VOCAB_DATASET_ID);

  if (dsP == NULL)
    return "@none";

  const char* dsId = dsP->value.s;

  kjChildRemove(attrP, dsP);
  return dsId;
}



// -----------------------------------------------------------------------------
//
// isCoreAttrTerm - is this node a structural attribute member (NOT a sub-attribute)?
//
// KJF_ATTR_TERM is classified once at core-context load and copied onto each node
// during corLdExpandTree (and stamped on the structural keys ldNormalizeInput
// creates). A single bit test instead of a strcmp chain.
//
static bool isCoreAttrTerm(const KjNode* nodeP)
{
  return ((nodeP->flags & KJF_ATTR_TERM) != 0);
}



// -----------------------------------------------------------------------------
//
// attrToDbModel - transform an attribute instance to DB format
//
// Adds timestamps and recurses into sub-attributes.
// Sub-attributes are children that are KjObject and NOT core context terms.
//
static void attrToDbModel(KjNode* attrP, uint64_t ts, KAlloc* faP)
{
  if (attrP->type != KjObject)
    return;

  // Recurse into sub-attributes (non-core-context object children)
  for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (childP->type == KjObject && !isCoreAttrTerm(childP))
      attrToDbModel(childP, ts, faP);
  }

  // For GeoProperty, un-expand the GeoJSON hasValue so MongoDB can use 2dsphere index
  if (ldAttrTypeDetect(attrP) == LdAttrGeoProperty)
  {
    KjNode* hasValueP = kjLookup(attrP, LD_VOCAB_HAS_VALUE);
    if (hasValueP != NULL)
      ldGeoValueUnexpand(hasValueP);
  }

  // Normalize value key: rename any HAS_* expanded IRI to "value" for uniform DB queries
  normalizeValueKey(attrP);

  // Convert observedAt ISO string to nanoseconds integer
  temporalPropertiesToNanoseconds(attrP);

  // Add timestamps to this attribute instance
  timestampSet(attrP, ts, ts, faP);
}



// -----------------------------------------------------------------------------
//
// wrapSingleAttr - wrap a single attribute object in a dataset-keyed wrapper
//
// Before: "attrName": { "type": "Property", "value": 100, "datasetId": "urn:x" }
// After:  "attrName": { "urn:x": { "type": "Property", "value": 100 } }
//
static KjNode* wrapSingleAttr(KjNode* attrP, uint64_t ts, KAlloc* faP)
{
  const char* dsKey = extractDatasetId(attrP);

  attrToDbModel(attrP, ts, faP);

  // Create wrapper object with same name as the attribute
  KjNode* wrapperP = kjObject(corRest.kjsonP, attrP->name);

  // Move attrP into the wrapper as a child keyed by datasetId
  // Keep attrP->next intact — kjChildReplace needs it to link wrapperP to the next sibling
  attrP->name = (char*) dsKey;
  wrapperP->value.firstChildP = attrP;
  wrapperP->lastChild         = attrP;

  return wrapperP;
}



// -----------------------------------------------------------------------------
//
// wrapMultiAttr - wrap multi-attribute array into a dataset-keyed wrapper object
//
// Before: "attrName": [ { "type": "Property", "value": 100 },
//                        { "type": "Property", "value": 98, "datasetId": "urn:x" } ]
// After:  "attrName": { "@none": { "type": "Property", "value": 100 },
//                        "urn:x": { "type": "Property", "value": 98 } }
//
static KjNode* wrapMultiAttr(KjNode* arrayP, uint64_t ts, KAlloc* faP)
{
  //
  // An array of non-objects is not a multi-attribute: it is the simplified value
  // of a ListProperty / ListRelationship / LanguageProperty on a Merge Entity that
  // declared ?format=simplified (§ 5.3.2.4, § 10.2.9.4). Leave it raw — exactly as
  // a bare scalar is left raw — for ldEntityMerge to shape against the type of the
  // pre-existing Attribute. Wrapping it produced a dataset-keyed object whose
  // "instances" were bare numbers, and the RFC 7396 merge walked one as if it had
  // children.
  //
  // Any other request has already had such an array wrapped as a Property by
  // ldNormalizeInput, so it cannot reach here; a genuine multi-attribute always
  // leads with an object, and one that turns non-object further along is still
  // rejected by ldCheckEntity.
  //
  if (arrayP->value.firstChildP != NULL && arrayP->value.firstChildP->type != KjObject)
    return NULL;

  KjNode* wrapperP = kjObject(corRest.kjsonP, arrayP->name);

  // Move each array element into the wrapper, keyed by its datasetId
  KjNode* instP = arrayP->value.firstChildP;

  while (instP != NULL)
  {
    KjNode* nextP = instP->next;

    const char* dsKey = extractDatasetId(instP);

    attrToDbModel(instP, ts, faP);

    instP->name = (char*) dsKey;
    instP->next = NULL;
    kjChildAdd(wrapperP, instP);

    instP = nextP;
  }

  return wrapperP;
}



// -----------------------------------------------------------------------------
//
// ldApiEntityToDbModel - transform API-format entity tree to DB storage format
//
void ldApiEntityToDbModel(KjNode* entityP, KAlloc* faP, int64_t createdAt)
{
  if (entityP == NULL || entityP->type != KjObject)
    return;

  uint64_t ts = corRest.requestStartTime;

  KjNode* childP = entityP->value.firstChildP;

  while (childP != NULL)
  {
    KjNode* nextP = childP->next;

    if (childP->name != NULL && !ldIsEntityKeyword(childP->name))
    {
      KjNode* replacementP = NULL;

      if (childP->type == KjObject)
        replacementP = wrapSingleAttr(childP, ts, faP);
      else if (childP->type == KjArray)
        replacementP = wrapMultiAttr(childP, ts, faP);

      if (replacementP != NULL)
      {
        kjChildReplace(entityP, childP, replacementP);
        childP->next = NULL;  // childP is now inside the wrapper
      }
    }

    childP = nextP;
  }

  // Convert entity-level expiresAt from ISO string to nanoseconds
  // After JSON-LD expansion, DateTime-typed values may be:
  //   - KjString: bare string (no expansion of value)
  //   - KjObject: {"@value": "2026-...", "@type": "DateTime"} (expanded by JSON-LD)
  for (KjNode* cP = entityP->value.firstChildP; cP != NULL; cP = cP->next)
  {
    if (strcmp(cP->name, LD_VOCAB_EXPIRES_AT) != 0)
      continue;

    if (cP->type == KjString)
    {
      //
      // The NGSI-LD Null is not a DateTime - it is the request to delete the member (§ 5.4.1),
      // and it has to reach the merge as itself. Converting it would turn "delete this" into
      // "expires at epoch zero".
      //
      if (strcmp(cP->value.s, LD_VOCAB_NGSILD_NULL) == 0)
        continue;

      cP->value.i = isoToNanoseconds(cP->value.s);
      cP->type    = KjInt;
    }
    else if (cP->type == KjObject)
    {
      // Extract @value from the expanded DateTime object
      KjNode* atValueP = NULL;
      for (KjNode* m = cP->value.firstChildP; m != NULL; m = m->next)
      {
        if (strcmp(m->name, "@value") == 0 && m->type == KjString)
        {
          atValueP = m;
          break;
        }
      }
      if (atValueP != NULL)
      {
        // Collapse the object to a plain integer
        cP->value.i = isoToNanoseconds(atValueP->value.s);
        cP->type    = KjInt;
      }
    }
    break;
  }

  // Add timestamps to the entity itself. createdAt > 0 means preserve a stored
  // value across a Replace (§ 6.5.3.3); 0 means this is a create — stamp 'now'.
  uint64_t entityCreatedAt = (createdAt > 0) ? (uint64_t) createdAt : ts;
  timestampSet(entityP, entityCreatedAt, ts, faP);
}
