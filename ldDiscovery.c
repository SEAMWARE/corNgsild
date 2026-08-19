//
// FILE            ldDiscovery.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
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

#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjBuilder.h"                            // kjArray, kjObject, kjString, kjInteger, kjChildAdd
#include "kjson/kjLookup.h"                             // kjLookup
#include "corRest/CorRestState.h"                         // corRest

#include "corNgsild/LdRegCache.h"                        // LdRegCache, LdRegCacheItem, LdRegInfo, LdRegEntityInfo, LdRegMode
#include "corNgsild/ldDiscovery.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// stringArrayAddUnique -
//
static void stringArrayAddUnique(KjNode* arr, const char* s)
{
  for (KjNode* p = arr->value.firstChildP; p != NULL; p = p->next)
    if (p->type == KjString && strcmp(p->value.s, s) == 0)
      return;
  kjChildAdd(arr, kjString(corRest.kjsonP, NULL, s));
}



// -----------------------------------------------------------------------------
//
// typeEntryEnsure -
//
static KjNode* typeEntryEnsure(KjNode* agg, const char* typeIri, bool details)
{
  for (KjNode* e = agg->value.firstChildP; e != NULL; e = e->next)
  {
    KjNode* iriP = kjLookup(e, "typeIri");
    if (iriP != NULL && iriP->type == KjString && strcmp(iriP->value.s, typeIri) == 0)
      return e;
  }

  KjNode* e = kjObject(corRest.kjsonP, NULL);
  kjChildAdd(e, kjString(corRest.kjsonP, "typeIri", typeIri));
  kjChildAdd(e, kjArray(corRest.kjsonP, "attrs"));
  if (details)
  {
    kjChildAdd(e, kjObject(corRest.kjsonP,  "attrTypes"));
    kjChildAdd(e, kjInteger(corRest.kjsonP, "entityCount", 0));
  }
  kjChildAdd(agg, e);
  return e;
}



// -----------------------------------------------------------------------------
//
// attrEntryEnsure -
//
static KjNode* attrEntryEnsure(KjNode* agg, const char* attrIri, bool details)
{
  for (KjNode* e = agg->value.firstChildP; e != NULL; e = e->next)
  {
    KjNode* iriP = kjLookup(e, "attrIri");
    if (iriP != NULL && iriP->type == KjString && strcmp(iriP->value.s, attrIri) == 0)
      return e;
  }

  KjNode* e = kjObject(corRest.kjsonP, NULL);
  kjChildAdd(e, kjString(corRest.kjsonP, "attrIri", attrIri));
  if (details)
  {
    kjChildAdd(e, kjArray(corRest.kjsonP,  "typeNames"));
    kjChildAdd(e, kjArray(corRest.kjsonP,  "attrTypes"));
    kjChildAdd(e, kjInteger(corRest.kjsonP, "attrCount", 0));
  }
  kjChildAdd(agg, e);
  return e;
}



// -----------------------------------------------------------------------------
//
// addAttrType -
//
static void addAttrType(KjNode* entry, const char* attrName, const char* at)
{
  KjNode* attrTypesObj = kjLookup(entry, "attrTypes");
  if (attrTypesObj == NULL) return;

  KjNode* atArr = kjLookup(attrTypesObj, attrName);
  if (atArr == NULL)
  {
    atArr = kjArray(corRest.kjsonP, attrName);
    kjChildAdd(attrTypesObj, atArr);
  }
  stringArrayAddUnique(atArr, at);
}



// -----------------------------------------------------------------------------
//
// ldDiscoveryRegAugmentTypes -
//
void ldDiscoveryRegAugmentTypes(KjNode* agg, LdRegCache* cacheP, bool details)
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

        KjNode* te = typeEntryEnsure(agg, ei->type, details);

        KjNode* attrs = kjLookup(te, "attrs");
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
void ldDiscoveryRegAugmentAttrs(KjNode* agg, LdRegCache* cacheP, bool details)
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
        KjNode* ae       = attrEntryEnsure(agg, attrIris[a], details);

        if (!details) continue;

        KjNode* typeArr  = kjLookup(ae, "typeNames");
        KjNode* atArr    = kjLookup(ae, "attrTypes");

        for (LdRegEntityInfo* ei = ri->entityInfoV; ei != NULL; ei = ei->next)
          if (ei->type != NULL)
            stringArrayAddUnique(typeArr, ei->type);

        stringArrayAddUnique(atArr, attrKinds[a]);
      }
    }
  }
}
