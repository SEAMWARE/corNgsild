//
// FILE            ldEntityToApi.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdbool.h>                                     // bool
#include <stdio.h>                                       // snprintf
#include <string.h>                                      // strcmp
#include <time.h>                                        // gmtime_r, strftime
#include "swRest/swRest.h"                            // swRest

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjBuilder.h"                              // kjArray
#include "kjson/kjChildReplace.h"                       // kjChildReplace
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_DATASET_ID, LD_VOCAB_SCOPE
#include "swNgsild/ldTypes.h"                            // ldAttrTypeFromString, ldValueKeyForType

#include "swNgsild/ldIsEntityKeyword.h"                   // ldIsEntityKeyword
#include "swNgsild/ldEntityToApi.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// timestampToIso - convert epoch nanoseconds (long long) to ISO 8601 string
//
// Output: "2026-04-01T12:00:00.000000000Z" (30 chars + null)
//
static void timestampToIso(long long nsec, char* buf, int bufSize)
{
  time_t     sec  = (time_t)(nsec / 1000000000LL);
  int        frac = (int)(nsec % 1000000000LL);
  struct tm  tm;

  gmtime_r(&sec, &tm);
  int n = strftime(buf, bufSize, "%Y-%m-%dT%H:%M:%S", &tm);

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
// timestampsToIsoStrings - convert createdAt/modifiedAt from integer to ISO string
//
// Walks the children of an object, finds createdAt/modifiedAt integer nodes,
// and converts them in-place to string nodes.
//
static void timestampsToIsoStrings(KjNode* objP, KAlloc* allocP)
{
  if (objP == NULL || objP->type != KjObject)
    return;

  for (KjNode* childP = objP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (childP->type == KjInt &&
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
        childP->type    = KjString;
        childP->value.s = isoStr;
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
static void restoreValueKey(KjNode* instP)
{
  if (instP->type != KjObject)
    return;

  KjNode* typeP  = NULL;
  KjNode* valueP = NULL;

  for (KjNode* childP = instP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, "type") == 0 && childP->type == KjString)  typeP  = childP;
    if (strcmp(childP->name, "value") == 0)                             valueP = childP;
  }

  if (typeP != NULL && valueP != NULL)
  {
    LdAttrType  aType      = ldAttrTypeFromString(typeP->value.s);
    const char* correctKey = ldValueKeyForType(aType);

    if (correctKey != NULL)
      valueP->name = (char*) correctKey;
  }

  // Recurse into sub-attributes (objects that have a "type" field with a known attr type)
  for (KjNode* childP = instP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (childP->type != KjObject)
      continue;

    // Check if this child is a sub-attribute by looking for a "type" field with a known attr type
    for (KjNode* gcP = childP->value.firstChildP; gcP != NULL; gcP = gcP->next)
    {
      if (strcmp(gcP->name, "type") == 0 && gcP->type == KjString && ldAttrTypeFromString(gcP->value.s) != LdAttrNone)
      {
        restoreValueKey(childP);
        break;
      }
    }
  }
}



// -----------------------------------------------------------------------------
//
// childCount - count the children of a container node
//
static int childCount(KjNode* containerP)
{
  if (containerP->type != KjObject && containerP->type != KjArray)
    return 0;

  int count = 0;

  for (KjNode* p = containerP->value.firstChildP; p != NULL; p = p->next)
    ++count;

  return count;
}



// -----------------------------------------------------------------------------
//
// ldEntityToApi - transform storage-format entity tree to API-format
//
// Every entity-level KjObject child (non-keyword) is a dataset-keyed wrapper.
// Unwrap them back to NGSI-LD API format:
//   - Single "@none" key  -> plain attribute object (no datasetId field)
//   - Single named key    -> plain attribute object with datasetId field
//   - Multiple keys       -> array of attribute objects with datasetId fields
//
void ldEntityToApi(KjNode* entityP, KAlloc* faP)
{
  if (entityP == NULL || entityP->type != KjObject)
    return;

  KjNode* childP = entityP->value.firstChildP;

  while (childP != NULL)
  {
    KjNode* nextP = childP->next;

    // Only process entity-level attribute wrappers (KjObject, non-keyword)
    if (childP->type != KjObject || childP->name == NULL || ldIsEntityKeyword(childP->name))
    {
      childP = nextP;
      continue;
    }

    int nInstances = childCount(childP);

    if (nInstances == 1)
    {
      // Single instance — unwrap to plain object
      KjNode* instP = childP->value.firstChildP;

      if (instP == NULL || instP->type != KjObject)
      {
        childP = nextP;
        continue;
      }

      // Restore normalized "value" key to correct expanded IRI
      restoreValueKey(instP);

      // If the key is not "@none", add datasetId back to the instance
      if (instP->name != NULL && strcmp(instP->name, "@none") != 0)
      {
        KjNode* dsNodeP = kjString(swRest.kjsonP, LD_VOCAB_DATASET_ID, instP->name);
        kjChildAdd(instP, dsNodeP);
      }

      // Unwrap: replace wrapper with the instance, keeping the attribute name
      instP->name = childP->name;
      instP->next = nextP;
      kjChildReplace(entityP, childP, instP);

      if (entityP->lastChild == childP)
        entityP->lastChild = instP;
    }
    else if (nInstances > 1)
    {
      // Multiple instances — build array
      KjNode* arrayP = kjArray(swRest.kjsonP, childP->name);

      KjNode* instP = childP->value.firstChildP;
      while (instP != NULL)
      {
        KjNode* instNextP = instP->next;

        // Restore normalized "value" key to correct expanded IRI
        restoreValueKey(instP);

        // Add datasetId back for named instances (not @none)
        if (instP->type == KjObject && instP->name != NULL && strcmp(instP->name, "@none") != 0)
        {
          KjNode* dsNodeP = kjString(swRest.kjsonP, LD_VOCAB_DATASET_ID, instP->name);
          kjChildAdd(instP, dsNodeP);
        }

        instP->name = NULL;  // array elements have no name
        instP->next = NULL;
        kjChildAdd(arrayP, instP);

        instP = instNextP;
      }

      arrayP->next = nextP;
      kjChildReplace(entityP, childP, arrayP);

      if (entityP->lastChild == childP)
        entityP->lastChild = arrayP;
    }

    childP = nextP;
  }

  // Convert integer timestamps to ISO 8601 strings (entity-level + inside each attribute)
  timestampsToIsoStrings(entityP, faP);

  for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    if (attrP->type == KjObject)
      timestampsToIsoStrings(attrP, faP);
    else if (attrP->type == KjArray)
    {
      for (KjNode* instP = attrP->value.firstChildP; instP != NULL; instP = instP->next)
      {
        if (instP->type == KjObject)
          timestampsToIsoStrings(instP, faP);
      }
    }
  }
}
