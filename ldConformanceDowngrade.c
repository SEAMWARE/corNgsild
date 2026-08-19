//
// FILE            ldConformanceDowngrade.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// § 4.3.6.8 backwards-compatibility transformations for outbound payloads.
//
#include <stdbool.h>                                     // bool
#include <stdio.h>                                       // sscanf
#include <string.h>                                      // strcmp, strchr

#include "kjson/KjNode.h"                                // KjNode, KjType
#include "kjson/kjson.h"                                 // Kjson
#include "kjson/kjBuilder.h"                             // kjString, kjChildAdd, kjChildRemove
#include "kjson/kjLookup.h"                              // kjLookup
#include "kjson/kjChildReplace.h"                        // kjChildReplace

#include "corNgsild/ldConformanceDowngrade.h"             // Own interface



// -----------------------------------------------------------------------------
//
// ldConformanceParse -
//
bool ldConformanceParse(const char* str, short* majorOut, short* minorOut)
{
  if (str == NULL) return false;

  int  major = 0;
  int  minor = 0;
  int  consumed = 0;
  int  n = sscanf(str, "%d.%d%n", &major, &minor, &consumed);
  if (n != 2 || str[consumed] != '\0')
    return false;
  if (major < 0 || major > 32767 || minor < 0 || minor > 32767)
    return false;

  if (majorOut != NULL) *majorOut = (short) major;
  if (minorOut != NULL) *minorOut = (short) minor;
  return true;
}



// olderThan - true if (target.major, target.minor) < (cmpMajor, cmpMinor)
//
static bool olderThan(short targetMajor, short targetMinor, short cmpMajor, short cmpMinor)
{
  if (targetMajor < cmpMajor) return true;
  if (targetMajor > cmpMajor) return false;
  return targetMinor < cmpMinor;
}



// attrType - read "type" from an attribute / instance object
//
static const char* attrType(KjNode* attrP)
{
  KjNode* tP = kjLookup(attrP, "type");
  if (tP != NULL && tP->type == KjString)
    return tP->value.s;
  return NULL;
}



// reformatAttr - apply per-version "reformat as Property/Relationship" fallback
//
// LanguageProperty → Property: rename "languageMap" to "value"
// JsonProperty     → Property: rename "json" to "value"
// VocabProperty    → Property: rename "vocab" to "value"
// ListProperty     → Property: rename "valueList" to "value"
// ListRelationship → Relationship: rename "objectList" to "object" (single-string
//                    or array — keep as-is; the consumer treats it as Relationship)
//
static void reformatAttr(KjNode* attrP)
{
  if (attrP == NULL || attrP->type != KjObject)
    return;

  KjNode* typeP = kjLookup(attrP, "type");
  if (typeP == NULL || typeP->type != KjString)
    return;

  const char* t = typeP->value.s;
  const char* renameKey  = NULL;
  const char* renameDest = NULL;
  const char* newType    = NULL;

  if      (strcmp(t, "LanguageProperty") == 0) { renameKey = "languageMap"; renameDest = "value";  newType = "Property"; }
  else if (strcmp(t, "JsonProperty")     == 0) { renameKey = "json";        renameDest = "value";  newType = "Property"; }
  else if (strcmp(t, "VocabProperty")    == 0) { renameKey = "vocab";       renameDest = "value";  newType = "Property"; }
  else if (strcmp(t, "ListProperty")     == 0) { renameKey = "valueList";   renameDest = "value";  newType = "Property"; }
  else if (strcmp(t, "ListRelationship") == 0) { renameKey = "objectList";  renameDest = "object"; newType = "Relationship"; }
  else
    return;

  KjNode* srcP = kjLookup(attrP, renameKey);
  if (srcP != NULL)
    srcP->name = (char*) renameDest;

  typeP->value.s = (char*) newType;
}



// stripField - remove a named child from a KjObject if present
//
static void stripField(KjNode* parentP, const char* name)
{
  if (parentP == NULL || parentP->type != KjObject || name == NULL) return;
  KjNode* p = kjLookup(parentP, name);
  if (p != NULL)
    kjChildRemove(parentP, p);
}



// downgradeAttrInstance - apply attr-level fallbacks to a single instance object
//
// (Both single-attr and multi-instance array elements share the same shape.)
//
static void downgradeAttrInstance(KjNode* instP, short tMajor, short tMinor)
{
  if (instP == NULL || instP->type != KjObject) return;

  // < 1.9: strip attr expiresAt, valueType
  if (olderThan(tMajor, tMinor, 1, 9))
  {
    stripField(instP, "expiresAt");
    stripField(instP, "valueType");
  }

  // < 1.8: reformat 1.8+ attribute kinds
  if (olderThan(tMajor, tMinor, 1, 8))
  {
    const char* t = attrType(instP);
    if (t != NULL && (strcmp(t, "JsonProperty") == 0 ||
                      strcmp(t, "VocabProperty") == 0 ||
                      strcmp(t, "ListProperty") == 0 ||
                      strcmp(t, "ListRelationship") == 0))
      reformatAttr(instP);
  }

  // < 1.4: reformat LanguageProperty
  if (olderThan(tMajor, tMinor, 1, 4))
  {
    const char* t = attrType(instP);
    if (t != NULL && strcmp(t, "LanguageProperty") == 0)
      reformatAttr(instP);
  }

  // < 1.3: strip attr-level datasetId, observedAt, unitCode
  if (olderThan(tMajor, tMinor, 1, 3))
  {
    stripField(instP, "datasetId");
    stripField(instP, "observedAt");
    stripField(instP, "unitCode");
  }
}



// downgradeAttr - apply downgrade to an attribute (which may be a single
// instance or a multi-instance array); for < 1.3, collapse multi-instance
// arrays to a single instance object (lifted in place of the array — pre-1.3
// didn't have attribute arrays at all).
//
static void downgradeAttr(KjNode* entityP, KjNode* attrP, short tMajor, short tMinor)
{
  if (attrP == NULL) return;

  if (attrP->type == KjArray)
  {
    // Per-instance downgrade first
    for (KjNode* instP = attrP->value.firstChildP; instP != NULL; instP = instP->next)
      downgradeAttrInstance(instP, tMajor, tMinor);

    // < 1.3: lift first instance object in place of the array.
    if (olderThan(tMajor, tMinor, 1, 3))
    {
      KjNode* firstP = attrP->value.firstChildP;
      if (firstP != NULL && firstP->type == KjObject && entityP != NULL)
      {
        firstP->name = attrP->name;
        firstP->next = NULL;
        kjChildReplace(entityP, attrP, firstP);
      }
    }
  }
  else
  {
    downgradeAttrInstance(attrP, tMajor, tMinor);
  }
}



// downgradeEntity - apply entity-level + per-attr downgrades
//
static void downgradeEntity(KjNode* entityP, short tMajor, short tMinor)
{
  if (entityP == NULL || entityP->type != KjObject) return;

  // < 1.9: strip entity-level expiresAt
  if (olderThan(tMajor, tMinor, 1, 9))
    stripField(entityP, "expiresAt");

  // < 1.4: strip entity-level scope
  if (olderThan(tMajor, tMinor, 1, 4))
    stripField(entityP, "scope");

  // < 1.3: collapse multi-type to first element
  if (olderThan(tMajor, tMinor, 1, 3))
  {
    KjNode* typeP = kjLookup(entityP, "type");
    if (typeP != NULL && typeP->type == KjArray && typeP->value.firstChildP != NULL)
    {
      KjNode* firstP = typeP->value.firstChildP;
      if (firstP->type == KjString)
      {
        // Replace the array node with a string node holding firstP's value
        typeP->type    = KjString;
        typeP->value.s = firstP->value.s;
      }
    }
  }

  // Recurse into attrs. downgradeAttr may replace a child (multi-instance
  // → single object collapse on < 1.3); capture next pointer up front so
  // iteration is safe across replacement.
  KjNode* attrP = entityP->value.firstChildP;
  while (attrP != NULL)
  {
    KjNode* nextP = attrP->next;

    bool skip = (attrP->name == NULL || attrP->name[0] == '@')              ||
                (strcmp(attrP->name, "id")         == 0)                    ||
                (strcmp(attrP->name, "type")       == 0)                    ||
                (strcmp(attrP->name, "scope")      == 0)                    ||
                (strcmp(attrP->name, "expiresAt")  == 0)                    ||
                (strcmp(attrP->name, "createdAt")  == 0)                    ||
                (strcmp(attrP->name, "modifiedAt") == 0);
    if (!skip)
      downgradeAttr(entityP, attrP, tMajor, tMinor);

    attrP = nextP;
  }
}



// ldConformanceDowngrade -
//
void ldConformanceDowngrade(KjNode* treeP, short targetMajor, short targetMinor, Kjson* kjsonP)
{
  (void) kjsonP;  // reserved for future replacement allocations

  if (treeP == NULL) return;
  if (targetMajor == 0 && targetMinor == 0) return;  // unset → no-op

  if (treeP->type == KjArray)
  {
    for (KjNode* entP = treeP->value.firstChildP; entP != NULL; entP = entP->next)
      downgradeEntity(entP, targetMajor, targetMinor);
  }
  else if (treeP->type == KjObject)
  {
    downgradeEntity(treeP, targetMajor, targetMinor);
  }
}
