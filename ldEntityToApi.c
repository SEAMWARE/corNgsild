//
// FILE            ldEntityToApi.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include <stdbool.h>                                     // bool
#include <stdio.h>                                       // snprintf
#include <string.h>                                      // strcmp
#include <time.h>                                        // gmtime_r, strftime
#include "corRest/corRest.h"                            // corRest

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "corTree/CorNode.h"                            // CorNode
#include "corTree/corTreeBuilder.h"                       // corTreeArray
#include "corTree/corTreeChildReplace.h"                // corTreeChildReplace
#include "corNgsild/LdVocab.h"                            // LD_VOCAB_DATASET_ID, LD_VOCAB_SCOPE
#include "corNgsild/ldTypes.h"                            // ldAttrTypeFromString, ldValueKeyForType

#include "corNgsild/ldIsEntityKeyword.h"                   // ldIsEntityKeyword
#include "corNgsild/ldEntityToApi.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// timestampToIso - convert epoch nanoseconds (long long) to ISO 8601 string
//
// Output: "2026-04-01T12:00:00.000000000Z" (30 chars + null)
//
static void timestampToIso(long long nsec, char* buf, int bufSize)
{
  extern bool ldTimestampHighPrecision;  // §5.2.2.4: 6 fractional digits by default, 9 with -hp

  time_t     sec  = (time_t)(nsec / 1000000000LL);
  int        frac = (int)(nsec % 1000000000LL);
  struct tm  tm;

  gmtime_r(&sec, &tm);
  int n = strftime(buf, bufSize, "%Y-%m-%dT%H:%M:%S", &tm);

  // NGSI-LD (§ 5.2.2.4) caps DateTime at 6 fractional digits (microseconds). Drop the
  // sub-microsecond part so the trailing-zero trim below yields at most 6 digits; -hp keeps all 9.
  if (!ldTimestampHighPrecision)
    frac = (frac / 1000) * 1000;

  if (frac == 0)
  {
    buf[n++] = 'Z';
    buf[n]   = 0;
  }
  else
  {
    snprintf(buf + n, bufSize - n, ".%09d", frac);
    // Trim trailing zeros
    int end = strlen(buf) - 1;
    while (end > n && buf[end] == '0')
      end--;
    buf[end + 1] = 'Z';
    buf[end + 2] = 0;
  }
}



// -----------------------------------------------------------------------------
//
// isValueKey - is this the node holding the attribute's VALUE?
//
// One name per attribute type, and the same seven whichever pass has run: the DB
// stores every value under "value", and restoreValueKey renames it to the type's
// own key, which is one of these too.
//
// It matters because a Property's value is OPAQUE USER JSON. An object in there
// may perfectly well carry a "type" member naming an NGSI-LD attribute type - an
// IoT Agent that copies its own provisioning record into a value writes exactly
// that - and the sub-attribute walks below would then rewrite user data as if it
// were structure. TS 104-175 § 5.2.2.5: implementations "shall preserve the
// representation of the content of the values ... and return the original
// content when replying to context consumption requests".
//
// A user's own sub-attribute cannot collide with these: sub-attribute names are
// expanded IRIs by the time they are stored, so only the structural keys are
// still spelled short.
//
static bool isValueKey(const char* name)
{
  if (name == NULL)                                   return false;

  if (strcmp(name, LD_VOCAB_HAS_VALUE)        == 0)   return true;   // "value"
  if (strcmp(name, LD_VOCAB_HAS_OBJECT)       == 0)   return true;   // "object"
  if (strcmp(name, LD_VOCAB_HAS_LANGUAGE_MAP) == 0)   return true;   // "languageMap"
  if (strcmp(name, LD_VOCAB_HAS_VOCAB)        == 0)   return true;   // "vocab"
  if (strcmp(name, LD_VOCAB_HAS_VALUE_LIST)   == 0)   return true;   // "valueList"
  if (strcmp(name, LD_VOCAB_HAS_OBJECT_LIST)  == 0)   return true;   // "objectList"
  if (strcmp(name, LD_VOCAB_HAS_JSON)         == 0)   return true;   // "json"

  return false;
}



// -----------------------------------------------------------------------------
//
// timestampsToIsoStrings - convert createdAt/modifiedAt from integer to ISO string
//
// Walks the children of an object, finds createdAt/modifiedAt integer nodes,
// and converts them in-place to string nodes.
//
static void timestampsToIsoStrings(CorNode* objP, KAlloc* allocP)
{
  if (objP == NULL || objP->type != CorObject)
    return;

  for (CorNode* childP = objP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (childP->type == CorInt &&
        (strcmp(childP->name, LD_VOCAB_CREATED_AT)  == 0 ||
         strcmp(childP->name, LD_VOCAB_MODIFIED_AT) == 0 ||
         strcmp(childP->name, LD_VOCAB_DELETED_AT)  == 0 ||
         strcmp(childP->name, LD_VOCAB_OBSERVED_AT) == 0 ||
         strcmp(childP->name, LD_VOCAB_EXPIRES_AT)  == 0))
    {
      char  isoBuf[32];
      timestampToIso(childP->value.i, isoBuf, sizeof(isoBuf));

      char* isoStr = (char*) kaAlloc(allocP, 32);
      if (isoStr != NULL)
      {
        strcpy(isoStr, isoBuf);
        childP->type    = CorString;
        childP->value.s = isoStr;
      }
    }
  }

  // Recurse into sub-attributes so their createdAt/modifiedAt/... convert too.
  // A sub-attribute is a CorObject child carrying a "type" of a known NGSI-LD
  // attribute type — and never the value node, whose contents are the user's
  // (see isValueKey). Without that second half an integer the user happened to
  // call "observedAt" inside a value came back as an ISO string.
  for (CorNode* childP = objP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (childP->type != CorObject)
      continue;

    if (isValueKey(childP->name))
      continue;

    for (CorNode* gcP = childP->value.firstChildP; gcP != NULL; gcP = gcP->next)
    {
      if (gcP->name != NULL && strcmp(gcP->name, "type") == 0 && gcP->type == CorString &&
          ldAttrTypeFromString(gcP->value.s) != LdAttrNone)
      {
        timestampsToIsoStrings(childP, allocP);
        break;
      }
    }
  }
}



// -----------------------------------------------------------------------------
//
// restoreValueKey - rename "value" back to the correct expanded IRI based on type
//
// In the DB, all attribute value keys are stored as "value" for uniform querying.
// On retrieval, restore the correct expanded IRI (hasValue, hasObject, etc.)
// based on the attribute's "type" field, so JSON-LD compaction produces the
// right short names.
//
static void restoreValueKey(CorNode* instP, bool collapseSingletonArrays)
{
  if (instP->type != CorObject)
    return;

  CorNode* typeP = NULL;
  CorNode* valueP = NULL;

  for (CorNode* childP = instP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, "type") == 0 && childP->type == CorString) typeP  = childP;
    if (strcmp(childP->name, "value") == 0)                             valueP = childP;
  }

  if (typeP != NULL && valueP != NULL)
  {
    LdAttrType  aType      = ldAttrTypeFromString(typeP->value.s);
    const char* correctKey = ldValueKeyForType(aType);

    if (correctKey != NULL)
      valueP->name = (char*) correctKey;

    //
    // JSON-LD compaction: a single-element array value of a term with no
    // @set/@list container compacts to the bare value. hasValue (Property)
    // and hasObject (Relationship) are such terms, so ["x"] -> "x". hasJSON
    // (@type:@json, an opaque literal), hasValueList / hasObjectList
    // (@container:@list) and hasLanguageMap (@container:@language) are NOT
    // collapsed - their arrays are significant and kept as-is.
    //
    // Skipped on the temporal path (collapseSingletonArrays == false): the
    // raw instance values feed ldToAggregatedValues / ldToTemporalValues,
    // where an array's cardinality is significant (e.g. array-length avg).
    //
    if (collapseSingletonArrays &&
        (aType == LdAttrProperty || aType == LdAttrRelationship) &&
        valueP->type == CorArray &&
        valueP->value.firstChildP != NULL &&
        valueP->value.firstChildP->next == NULL)
    {
      CorNode* onlyP    = valueP->value.firstChildP;
      valueP->type      = onlyP->type;
      valueP->value     = onlyP->value;
      valueP->lastChild = onlyP->lastChild;
    }
  }

  //
  // Recurse into sub-attributes (objects that have a "type" field with a known
  // attr type) - but NEVER into the value itself, see isValueKey.
  //
  // Walking it renamed the USER's "value" key to "vocab" / "languageMap" /
  // "valueList" / "json", and the entity came back altered. Property and
  // Relationship are why this hid: their rename produces hasValue / hasObject,
  // which compact straight back to "value" / "object", so only the types whose
  // value key is spelled differently ever showed it.
  //
  for (CorNode* childP = instP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (childP->type != CorObject)
      continue;

    if (isValueKey(childP->name))
      continue;

    // Check if this child is a sub-attribute by looking for a "type" field with a known attr type
    for (CorNode* gcP = childP->value.firstChildP; gcP != NULL; gcP = gcP->next)
    {
      if (strcmp(gcP->name, "type") == 0 && gcP->type == CorString && ldAttrTypeFromString(gcP->value.s) != LdAttrNone)
      {
        restoreValueKey(childP, collapseSingletonArrays);
        break;
      }
    }
  }
}



// -----------------------------------------------------------------------------
//
// childCount - count the children of a container node
//
static int childCount(CorNode* containerP)
{
  if (containerP->type != CorObject && containerP->type != CorArray)
    return 0;

  int count = 0;

  for (CorNode* p = containerP->value.firstChildP; p != NULL; p = p->next)
    ++count;

  return count;
}



// -----------------------------------------------------------------------------
//
// ldEntityToApi - transform storage-format entity tree to API-format
//
// Every entity-level CorObject child (non-keyword) is a dataset-keyed wrapper.
// Unwrap them back to NGSI-LD API format:
//   - Single "@none" key  -> plain attribute object (no datasetId field)
//   - Single named key    -> plain attribute object with datasetId field
//   - Multiple keys       -> array of attribute objects with datasetId fields
//
void ldEntityToApi(CorNode* entityP, KAlloc* faP)
{
  if (entityP == NULL || entityP->type != CorObject)
    return;

  CorNode* childP = entityP->value.firstChildP;

  while (childP != NULL)
  {
    CorNode* nextP = childP->next;

    // Skip entity keywords (id, type, scope, ...).
    if (childP->name == NULL || ldIsEntityKeyword(childP->name))
    {
      childP = nextP;
      continue;
    }

    // Temporal attribute: CorArray of instance objects (not the
    // dataset-keyed CorObject wrapper). ldEntityToApi's unwrap logic
    // doesn't apply, but each instance's "value" key still needs
    // restoring to the type-appropriate IRI (e.g. JsonProperty stores
    // `value` but the API names it `json`).
    if (childP->type == CorArray)
    {
      for (CorNode* instP = childP->value.firstChildP; instP != NULL; instP = instP->next)
        restoreValueKey(instP, false);  // temporal: keep raw array values for aggregation
      childP = nextP;
      continue;
    }

    // Otherwise must be the dataset-keyed wrapper (CorObject).
    if (childP->type != CorObject)
    {
      childP = nextP;
      continue;
    }

    int nInstances = childCount(childP);

    if (nInstances == 1)
    {
      // Single instance — unwrap to plain object
      CorNode* instP = childP->value.firstChildP;

      if (instP == NULL || instP->type != CorObject)
      {
        childP = nextP;
        continue;
      }

      // Restore normalized "value" key to correct expanded IRI
      restoreValueKey(instP, true);

      // If the key is not "@none", add datasetId back to the instance
      if (instP->name != NULL && strcmp(instP->name, "@none") != 0)
      {
        CorNode* dsNodeP = corTreeString(corRest.kallocP, LD_VOCAB_DATASET_ID, instP->name);
        corTreeChildAdd(instP, dsNodeP);
      }

      // Unwrap: replace wrapper with the instance, keeping the attribute name
      instP->name = childP->name;
      instP->next = nextP;
      corTreeChildReplace(entityP, childP, instP);

      if (entityP->lastChild == childP)
        entityP->lastChild = instP;
    }
    else if (nInstances > 1)
    {
      // Multiple instances — build array
      CorNode* arrayP = corTreeArray(corRest.kallocP, childP->name);

      CorNode* instP = childP->value.firstChildP;
      while (instP != NULL)
      {
        CorNode* instNextP = instP->next;

        // Restore normalized "value" key to correct expanded IRI
        restoreValueKey(instP, true);

        // Add datasetId back for named instances (not @none)
        if (instP->type == CorObject && instP->name != NULL && strcmp(instP->name, "@none") != 0)
        {
          CorNode* dsNodeP = corTreeString(corRest.kallocP, LD_VOCAB_DATASET_ID, instP->name);
          corTreeChildAdd(instP, dsNodeP);
        }

        instP->name = NULL;  // array elements have no name
        instP->next = NULL;
        corTreeChildAdd(arrayP, instP);

        instP = instNextP;
      }

      arrayP->next = nextP;
      corTreeChildReplace(entityP, childP, arrayP);

      if (entityP->lastChild == childP)
        entityP->lastChild = arrayP;
    }

    childP = nextP;
  }

  // Convert integer timestamps to ISO 8601 strings (entity-level + inside each attribute)
  timestampsToIsoStrings(entityP, faP);

  for (CorNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    if (attrP->type == CorObject)
      timestampsToIsoStrings(attrP, faP);
    else if (attrP->type == CorArray)
    {
      for (CorNode* instP = attrP->value.firstChildP; instP != NULL; instP = instP->next)
      {
        if (instP->type == CorObject)
          timestampsToIsoStrings(instP, faP);
      }
    }
  }
}
