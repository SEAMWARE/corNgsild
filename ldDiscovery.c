//
// FILE            ldDiscovery.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Discovery aggregation augmenters for § 5.7.11 modes 2/3:
// fold CSR-declared entity types and attributes (plus property/
// relationship hints) into the aggregated result produced by the DB
// plugin's typeList/attrList.
//
// The CSR does not expose per-type entity counts or per-attr instance
// counts — only the declared "possibly available" types and names.
// We therefore add missing entries, union the attribute lists, and
// seed attrTypes with "Property" / "Relationship" where hinted, but
// leave entityCount/attrCount reflecting local data only (spec
// allows approximate counts — § 5.7.11).
//

#include <string.h>                                     // strcmp

#include "corTree/CorNode.h"                            // CorNode
#include "corTree/corTreeBuilder.h"                     // corTreeArray, corTreeObject, corTreeString, corTreeInteger, corTreeChildAdd
#include "corTree/corTreeLookup.h"                      // corTreeLookup
#include "corRest/CorRestState.h"                         // corRest

#include "corNgsild/LdRegCache.h"                        // LdRegCache, LdRegCacheItem, LdRegInfo, LdRegEntityInfo, LdRegMode
#include "corNgsild/ldDiscovery.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// stringArrayAddUnique -
//
static void stringArrayAddUnique(CorNode* arr, const char* s)
{
  for (CorNode* p = arr->value.head; p != NULL; p = p->next)
    if (p->type == CorString && strcmp(p->value.s, s) == 0)
      return;
  corTreeChildAdd(arr, corTreeString(corRest.kallocP, NULL, s));
}



// -----------------------------------------------------------------------------
//
// typeEntryEnsure -
//
static CorNode* typeEntryEnsure(CorNode* agg, const char* typeIri, bool details)
{
  for (CorNode* e = agg->value.head; e != NULL; e = e->next)
  {
    CorNode* iriP = corTreeLookup(e, "typeIri");
    if (iriP != NULL && iriP->type == CorString && strcmp(iriP->value.s, typeIri) == 0)
      return e;
  }

  CorNode* e = corTreeObject(corRest.kallocP, NULL);
  corTreeChildAdd(e, corTreeString(corRest.kallocP, "typeIri", typeIri));
  corTreeChildAdd(e, corTreeArray(corRest.kallocP, "attrs"));
  if (details)
  {
    corTreeChildAdd(e, corTreeObject(corRest.kallocP, "attrTypes"));
    corTreeChildAdd(e, corTreeInteger(corRest.kallocP, "entityCount", 0));
  }
  corTreeChildAdd(agg, e);
  return e;
}



// -----------------------------------------------------------------------------
//
// attrEntryEnsure -
//
static CorNode* attrEntryEnsure(CorNode* agg, const char* attrIri, bool details)
{
  for (CorNode* e = agg->value.head; e != NULL; e = e->next)
  {
    CorNode* iriP = corTreeLookup(e, "attrIri");
    if (iriP != NULL && iriP->type == CorString && strcmp(iriP->value.s, attrIri) == 0)
      return e;
  }

  CorNode* e = corTreeObject(corRest.kallocP, NULL);
  corTreeChildAdd(e, corTreeString(corRest.kallocP, "attrIri", attrIri));
  if (details)
  {
    corTreeChildAdd(e, corTreeArray(corRest.kallocP, "typeNames"));
    corTreeChildAdd(e, corTreeArray(corRest.kallocP, "attrTypes"));
    corTreeChildAdd(e, corTreeInteger(corRest.kallocP, "attrCount", 0));
  }
  corTreeChildAdd(agg, e);
  return e;
}



// -----------------------------------------------------------------------------
//
// addAttrType -
//
static void addAttrType(CorNode* entry, const char* attrName, const char* at)
{
  CorNode* attrTypesObj = corTreeLookup(entry, "attrTypes");
  if (attrTypesObj == NULL) return;

  CorNode* atArr = corTreeLookup(attrTypesObj, attrName);
  if (atArr == NULL)
  {
    atArr = corTreeArray(corRest.kallocP, attrName);
    corTreeChildAdd(attrTypesObj, atArr);
  }
  stringArrayAddUnique(atArr, at);
}



// -----------------------------------------------------------------------------
//
// ldDiscoveryRegAugmentTypes -
//
void ldDiscoveryRegAugmentTypes(CorNode* agg, LdRegCache* cacheP, bool details)
{
  if (agg == NULL || cacheP == NULL) return;

  for (LdRegCacheItem* it = cacheP->itemList; it != NULL; it = it->next)
  {
    // Auxiliary CSRs advertise "possibly available" content like any other —
    // include them. Mode information only matters for retrieve/update
    // dispatch, not for what's declared available.
    (void) it->mode;

    for (LdRegInfo* ri = it->infoV; ri != NULL; ri = ri->next)
    {
      // Collect the types this RegInfo declares
      for (LdRegEntityInfo* ei = ri->entityInfoV; ei != NULL; ei = ei->next)
      {
        if (ei->type == NULL) continue;

        CorNode* te = typeEntryEnsure(agg, ei->type, details);

        CorNode* attrs = corTreeLookup(te, "attrs");
        // A registration's attributeNames carry no Property/Relationship
        // distinction (the split is deprecated), so discovery defaults the
        // reported attribute type to "Property".
        if (ri->attributeNamesV != NULL)
        {
          for (int i = 0; ri->attributeNamesV[i] != NULL; i++)
          {
            stringArrayAddUnique(attrs, ri->attributeNamesV[i]);
            if (details)
              addAttrType(te, ri->attributeNamesV[i], "Property");
          }
        }
      }
    }
  }
}



// -----------------------------------------------------------------------------
//
// ldDiscoveryRegAugmentAttrs -
//
void ldDiscoveryRegAugmentAttrs(CorNode* agg, LdRegCache* cacheP, bool details)
{
  if (agg == NULL || cacheP == NULL) return;

  for (LdRegCacheItem* it = cacheP->itemList; it != NULL; it = it->next)
  {
    for (LdRegInfo* ri = it->infoV; ri != NULL; ri = ri->next)
    {
      // Gather declared attr IRIs once per RegInfo
      const char* attrIris[256];
      const char* attrKinds[256];
      int         attrN = 0;

      // attributeNames carry no Property/Relationship distinction (deprecated
      // split) — discovery defaults the reported attribute type to "Property".
      if (ri->attributeNamesV != NULL)
      {
        for (int i = 0; ri->attributeNamesV[i] != NULL && attrN < 256; i++)
        {
          attrIris[attrN]  = ri->attributeNamesV[i];
          attrKinds[attrN] = "Property";
          attrN++;
        }
      }

      // Entity types this RegInfo declares
      for (int a = 0; a < attrN; a++)
      {
        CorNode* ae      = attrEntryEnsure(agg, attrIris[a], details);

        if (!details) continue;

        CorNode* typeArr = corTreeLookup(ae, "typeNames");
        CorNode* atArr   = corTreeLookup(ae, "attrTypes");

        for (LdRegEntityInfo* ei = ri->entityInfoV; ei != NULL; ei = ei->next)
          if (ei->type != NULL)
            stringArrayAddUnique(typeArr, ei->type);

        stringArrayAddUnique(atArr, attrKinds[a]);
      }
    }
  }
}
