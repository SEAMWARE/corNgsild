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

#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corTree/CorNode.h"                            // CorNode
#include "corTree/corTreeBuilder.h"                      // corTreeObject
#include "corTree/corTreeChildReplace.h"                // corTreeChildReplace
#include "corTree/corTreeLookup.h"                      // corTreeLookup
#include "corRest/corRest.h"                             // corRest

#include "corJsonld/corLdExpand.h"                          // KJF_ATTR_TERM
#include "corNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "corNgsild/LdAttrType.h"                         // LdAttrType, LdAttrGeoProperty
#include "corNgsild/ldAttrTypeDetect.h"                   // ldAttrTypeDetect
#include "corNgsild/ldIsEntityKeyword.h"                   // ldIsEntityKeyword
#include "corNgsild/ldCheckDateTime.h"                    // ldIsoToNanoseconds
#include "corNgsild/ldApiEntityToDbModel.h"               // Own interface
#include "corNgsild/ldTermId.h"                         // ldTermId, CorTerm*



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
static void ldGeoValueUnexpand(CorNode* geoValueP)
{
  if (geoValueP == NULL || geoValueP->type != CorObject)
    return;

  for (CorNode* childP = geoValueP->value.head; childP != NULL; childP = childP->next)
  {
    // Un-expand key names: "https://purl.org/geojson/vocab#coordinates" -> "coordinates"
    if (strncmp(childP->name, LD_VOCAB_GEOJSON_PREFIX, LD_VOCAB_GEOJSON_PREFIX_LEN) == 0)
      childP->name = childP->name + LD_VOCAB_GEOJSON_PREFIX_LEN;

    // Un-expand the "type" value: "https://purl.org/geojson/vocab#Point" -> "Point"
    if (ldTermId(childP) == CorTermType && childP->type == CorString)
    {
      if (strncmp(childP->value.s, LD_VOCAB_GEOJSON_PREFIX, LD_VOCAB_GEOJSON_PREFIX_LEN) == 0)
        childP->value.s = childP->value.s + LD_VOCAB_GEOJSON_PREFIX_LEN;
    }

    // Recurse into nested objects (e.g. GeometryCollection members)
    if (childP->type == CorObject)
      ldGeoValueUnexpand(childP);

    // Recurse into arrays of objects (e.g. GeometryCollection "geometries" array)
    if (childP->type == CorArray)
    {
      for (CorNode* elemP = childP->value.head; elemP != NULL; elemP = elemP->next)
      {
        if (elemP->type == CorObject)
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
// temporalPropertiesToNanoseconds - convert observedAt string children to CorInt nanoseconds
//
static void temporalPropertiesToNanoseconds(CorNode* attrP)
{
  for (CorNode* childP = attrP->value.head; childP != NULL; childP = childP->next)
  {
    bool isTemporal = (ldTermId(childP) == CorTermObservedAt ||
                       ldTermId(childP) == CorTermExpiresAt);
    if (!isTemporal)
      continue;

    if (childP->type == CorString)
    {
      // NGSI-LD Null marker: leave the bare string so a merge/update apply
      // removes the temporal sub-attribute (§ 5.4.1) — converting it here would
      // store epoch 0 instead. (A Create/Replace with a null observedAt is
      // already rejected earlier by ldCheckAttribute.)
      if (strcmp(childP->value.s, LD_VOCAB_NGSILD_NULL) == 0)
        continue;
      childP->value.i = isoToNanoseconds(childP->value.s);
      childP->type = CorInt;
    }
    else if (childP->type == CorObject)
    {
      // JSON-LD expanded DateTime: {"@value": "2026-...", "@type": "DateTime"}
      for (CorNode* m = childP->value.head; m != NULL; m = m->next)
      {
        if (strcmp(m->name, "@value") == 0 && m->type == CorString)
        {
          childP->value.i = isoToNanoseconds(m->value.s);
          childP->type = CorInt;
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
static void normalizeValueKey(CorNode* attrP)
{
  if (attrP->type != CorObject)
    return;

  for (CorNode* childP = attrP->value.head; childP != NULL; childP = childP->next)
    for (const char** vk = expandedValueKeys; *vk != NULL; vk++)
      if (strcmp(childP->name, *vk) == 0) { childP->name = "value"; return; }
}



// -----------------------------------------------------------------------------
//
// timestampSet - add createdAt/modifiedAt to an CorObject node
//
static void timestampSet(CorNode* objP, uint64_t createdAt, uint64_t modifiedAt, CorAlloc* faP)
{
  corTreeChildAdd(objP, corTreeInteger(corRest.kallocP, LD_VOCAB_CREATED_AT, (long long) createdAt));
  corTreeChildAdd(objP, corTreeInteger(corRest.kallocP, LD_VOCAB_MODIFIED_AT, (long long) modifiedAt));
}



// -----------------------------------------------------------------------------
//
// extractDatasetId - find and remove datasetId from an attribute instance
//
// Returns the datasetId value string, or "@none" if not present.
//
static const char* extractDatasetId(CorNode* attrP)
{
  CorNode* dsP = corTreeLookup(attrP, LD_VOCAB_DATASET_ID);

  if (dsP == NULL)
    return "@none";

  const char* dsId = dsP->value.s;

  corTreeChildRemove(attrP, dsP);
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
static bool isCoreAttrTerm(const CorNode* nodeP)
{
  return ((nodeP->flags & KJF_ATTR_TERM) != 0);
}



// -----------------------------------------------------------------------------
//
// attrToDbModel - transform an attribute instance to DB format
//
// Adds timestamps and recurses into sub-attributes.
// Sub-attributes are children that are CorObject and NOT core context terms.
//
static void attrToDbModel(CorNode* attrP, uint64_t ts, CorAlloc* faP)
{
  if (attrP->type != CorObject)
    return;

  // Recurse into sub-attributes (non-core-context object children)
  for (CorNode* childP = attrP->value.head; childP != NULL; childP = childP->next)
  {
    if (childP->type == CorObject && !isCoreAttrTerm(childP))
      attrToDbModel(childP, ts, faP);
  }

  // For GeoProperty, un-expand the GeoJSON hasValue so MongoDB can use 2dsphere index
  if (ldAttrTypeDetect(attrP) == LdAttrGeoProperty)
  {
    CorNode* hasValueP = corTreeLookup(attrP, LD_VOCAB_HAS_VALUE);
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
static CorNode* wrapSingleAttr(CorNode* attrP, uint64_t ts, CorAlloc* faP)
{
  const char* dsKey = extractDatasetId(attrP);

  attrToDbModel(attrP, ts, faP);

  // Create wrapper object with same name as the attribute
  CorNode* wrapperP = corTreeObject(corRest.kallocP, attrP->name);

  // Move attrP into the wrapper as a child keyed by datasetId
  // Keep attrP->next intact — corTreeChildReplace needs it to link wrapperP to the next sibling
  attrP->name = (char*) dsKey;
  wrapperP->value.head = attrP;
  wrapperP->value.tail        = attrP;

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
static CorNode* wrapMultiAttr(CorNode* arrayP, uint64_t ts, CorAlloc* faP)
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
  if (arrayP->value.head != NULL && arrayP->value.head->type != CorObject)
    return NULL;

  CorNode* wrapperP = corTreeObject(corRest.kallocP, arrayP->name);

  // Move each array element into the wrapper, keyed by its datasetId
  CorNode* instP = arrayP->value.head;

  while (instP != NULL)
  {
    CorNode* nextP = instP->next;

    const char* dsKey = extractDatasetId(instP);

    attrToDbModel(instP, ts, faP);

    instP->name = (char*) dsKey;
    instP->next = NULL;
    corTreeChildAdd(wrapperP, instP);

    instP = nextP;
  }

  return wrapperP;
}



// -----------------------------------------------------------------------------
//
// ldApiEntityToDbModel - transform API-format entity tree to DB storage format
//
void ldApiEntityToDbModel(CorNode* entityP, CorAlloc* faP, int64_t createdAt)
{
  if (entityP == NULL || entityP->type != CorObject)
    return;

  uint64_t ts = corRest.requestStartTime;

  CorNode* childP = entityP->value.head;

  while (childP != NULL)
  {
    CorNode* nextP = childP->next;

    if (childP->name != NULL && !ldIsEntityMember(childP))
    {
      CorNode* replacementP = NULL;

      if (childP->type == CorObject)
        replacementP = wrapSingleAttr(childP, ts, faP);
      else if (childP->type == CorArray)
        replacementP = wrapMultiAttr(childP, ts, faP);

      if (replacementP != NULL)
      {
        corTreeChildReplace(entityP, childP, replacementP);
        childP->next = NULL;  // childP is now inside the wrapper
      }
    }

    childP = nextP;
  }

  // Convert entity-level expiresAt from ISO string to nanoseconds
  // After JSON-LD expansion, DateTime-typed values may be:
  //   - CorString: bare string (no expansion of value)
  //   - CorObject: {"@value": "2026-...", "@type": "DateTime"} (expanded by JSON-LD)
  for (CorNode* cP = entityP->value.head; cP != NULL; cP = cP->next)
  {
    if (ldTermId(cP) != CorTermExpiresAt)
      continue;

    if (cP->type == CorString)
    {
      //
      // The NGSI-LD Null is not a DateTime - it is the request to delete the member (§ 5.4.1),
      // and it has to reach the merge as itself. Converting it would turn "delete this" into
      // "expires at epoch zero".
      //
      if (strcmp(cP->value.s, LD_VOCAB_NGSILD_NULL) == 0)
        continue;

      cP->value.i = isoToNanoseconds(cP->value.s);
      cP->type    = CorInt;
    }
    else if (cP->type == CorObject)
    {
      // Extract @value from the expanded DateTime object
      CorNode* atValueP = NULL;
      for (CorNode* m = cP->value.head; m != NULL; m = m->next)
      {
        if (strcmp(m->name, "@value") == 0 && m->type == CorString)
        {
          atValueP = m;
          break;
        }
      }
      if (atValueP != NULL)
      {
        // Collapse the object to a plain integer
        cP->value.i = isoToNanoseconds(atValueP->value.s);
        cP->type    = CorInt;
      }
    }
    break;
  }

  // Add timestamps to the entity itself. createdAt > 0 means preserve a stored
  // value across a Replace (§ 6.5.3.3); 0 means this is a create — stamp 'now'.
  uint64_t entityCreatedAt = (createdAt > 0) ? (uint64_t) createdAt : ts;
  timestampSet(entityP, entityCreatedAt, ts, faP);
}
