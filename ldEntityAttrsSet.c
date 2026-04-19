//
// FILE            ldEntityAttrsSet.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//

#include <string.h>                                   // strcmp

#include "kjson/KjNode.h"                             // KjNode
#include "kjson/kjBuilder.h"                          // kjObject, kjString, kjInteger, kjChildAdd, kjChildRemove
#include "kjson/kjLookup.h"                           // kjLookup
#include "kjson/kjClone.h"                            // kjClone
#include "swRest/swRest.h"                            // swRest (request-scoped allocator for the report)

#include "swNgsild/LdVocab.h"                         // LD_VOCAB_*
#include "swNgsild/ldEntityAttrsSet.h"                // Own interface



// -----------------------------------------------------------------------------
//
// isEntityKeyword - top-level node names that are not attributes
//
static bool isEntityKeyword(const char* name)
{
  if (name == NULL)              return true;
  if (name[0] == '@')            return true;
  if (strcmp(name, "id")   == 0) return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// bumpModifiedAt - set modifiedAt on an object (replace in-place or add)
//
static void bumpModifiedAt(KjNode* objP, uint64_t ts, Kjson* allocP)
{
  if (objP == NULL || objP->type != KjObject)
    return;

  KjNode* mP = kjLookup(objP, LD_VOCAB_MODIFIED_AT);
  if (mP != NULL)
  {
    mP->type    = KjInt;
    mP->value.i = (long long) ts;
    return;
  }
  kjChildAdd(objP, kjInteger(allocP, LD_VOCAB_MODIFIED_AT, (long long) ts));
}



// -----------------------------------------------------------------------------
//
// stampCreatedAtIfMissing -
//
static void stampCreatedAtIfMissing(KjNode* objP, uint64_t ts, Kjson* allocP)
{
  if (objP == NULL || objP->type != KjObject)
    return;

  if (kjLookup(objP, LD_VOCAB_CREATED_AT) == NULL)
    kjChildAdd(objP, kjInteger(allocP, LD_VOCAB_CREATED_AT, (long long) ts));
}



// -----------------------------------------------------------------------------
//
// addReportEntry - append one per-attr change record
//
// Matches LdMergeReport's shape (used by subscription matcher): each
// record is a KjObject with "attr", "reason", and optional "preValue".
// Report nodes live on the request-scoped allocator (swRest.kjsonP).
//
static void addReportEntry(LdMergeReport* reportP, const char* attrName,
                           const char* reason, KjNode* preValue)
{
  if (reportP == NULL)
    return;

  if (reportP->changes == NULL)
    reportP->changes = kjArray(swRest.kjsonP, "changes");

  KjNode* entry = kjObject(swRest.kjsonP, NULL);
  kjChildAdd(entry, kjString(swRest.kjsonP, "attr",   attrName));
  kjChildAdd(entry, kjString(swRest.kjsonP, "reason", reason));
  if (preValue != NULL)
  {
    KjNode* clone = kjClone(swRest.kjsonP, preValue);
    clone->name = (char*) "preValue";
    kjChildAdd(entry, clone);
  }
  kjChildAdd(reportP->changes, entry);
}



// -----------------------------------------------------------------------------
//
// applyType - union fragment's type values into target's type
//
// target's "type" may be KjString (single) or KjArray. Fragment's likewise.
// Result is KjString if only one value, else a KjArray containing all
// unique values.
//
static void applyType(KjNode* target, KjNode* fragType, Kjson* allocP)
{
  if (fragType == NULL)
    return;

  KjNode* tType = kjLookup(target, "type");

  //
  // Normalize target->type to a KjArray so we can append without caring
  // about the prior shape. Restore to KjString at the end if only one.
  //
  if (tType == NULL)
  {
    // Fragment's type becomes target's type wholesale (clone).
    KjNode* clone = kjClone(allocP, fragType);
    clone->name = (char*) "type";
    kjChildAdd(target, clone);
    return;
  }

  // Collect existing values into an array form
  KjNode* arrayP = kjArray(allocP, "type");
  if (tType->type == KjString)
  {
    kjChildAdd(arrayP, kjString(allocP, NULL, tType->value.s));
  }
  else if (tType->type == KjArray)
  {
    for (KjNode* c = tType->value.firstChildP; c != NULL; c = c->next)
      if (c->type == KjString)
        kjChildAdd(arrayP, kjString(allocP, NULL, c->value.s));
  }

  // Union in the fragment values (skip duplicates)
  const char* toAdd[32];
  int         toAddN = 0;
  if (fragType->type == KjString)
  {
    toAdd[toAddN++] = fragType->value.s;
  }
  else if (fragType->type == KjArray)
  {
    for (KjNode* c = fragType->value.firstChildP; c != NULL; c = c->next)
      if (c->type == KjString && toAddN < 32)
        toAdd[toAddN++] = c->value.s;
  }

  for (int i = 0; i < toAddN; i++)
  {
    bool present = false;
    for (KjNode* c = arrayP->value.firstChildP; c != NULL; c = c->next)
      if (c->type == KjString && strcmp(c->value.s, toAdd[i]) == 0) { present = true; break; }
    if (!present)
      kjChildAdd(arrayP, kjString(allocP, NULL, toAdd[i]));
  }

  // Replace target.type
  kjChildRemove(target, tType);

  // Count elements in arrayP
  int n = 0;
  for (KjNode* c = arrayP->value.firstChildP; c != NULL; c = c->next) n++;

  if (n == 1)
  {
    KjNode* only = arrayP->value.firstChildP;
    kjChildAdd(target, kjString(allocP, "type", only->value.s));
  }
  else
  {
    kjChildAdd(target, arrayP);
  }
}



// -----------------------------------------------------------------------------
//
// applyScope - replace or union fragment's scope into target's scope
//
static void applyScope(KjNode* target, KjNode* fragScope, bool overwrite, Kjson* allocP)
{
  if (fragScope == NULL)
    return;

  KjNode* tScope = kjLookup(target, LD_VOCAB_SCOPE);

  if (overwrite || tScope == NULL)
  {
    if (tScope != NULL)
      kjChildRemove(target, tScope);

    KjNode* clone = kjClone(allocP, fragScope);
    clone->name = (char*) LD_VOCAB_SCOPE;
    kjChildAdd(target, clone);
    return;
  }

  //
  // Union: merge fragment scope values into target scope (string or array).
  //
  KjNode* arrayP = kjArray(allocP, LD_VOCAB_SCOPE);
  if (tScope->type == KjString)
    kjChildAdd(arrayP, kjString(allocP, NULL, tScope->value.s));
  else if (tScope->type == KjArray)
    for (KjNode* c = tScope->value.firstChildP; c != NULL; c = c->next)
      if (c->type == KjString)
        kjChildAdd(arrayP, kjString(allocP, NULL, c->value.s));

  const char* toAdd[32];
  int         toAddN = 0;
  if (fragScope->type == KjString)
    toAdd[toAddN++] = fragScope->value.s;
  else if (fragScope->type == KjArray)
    for (KjNode* c = fragScope->value.firstChildP; c != NULL; c = c->next)
      if (c->type == KjString && toAddN < 32)
        toAdd[toAddN++] = c->value.s;

  for (int i = 0; i < toAddN; i++)
  {
    bool present = false;
    for (KjNode* c = arrayP->value.firstChildP; c != NULL; c = c->next)
      if (c->type == KjString && strcmp(c->value.s, toAdd[i]) == 0) { present = true; break; }
    if (!present)
      kjChildAdd(arrayP, kjString(allocP, NULL, toAdd[i]));
  }

  kjChildRemove(target, tScope);
  kjChildAdd(target, arrayP);
}



// -----------------------------------------------------------------------------
//
// ldEntityAttrsSet -
//
void ldEntityAttrsSet(KjNode* target, KjNode* fragment,
                      bool overwriteScope, uint64_t ts,
                      LdMergeReport* reportP, Kjson* targetAllocP)
{
  if (target == NULL || fragment == NULL || fragment->type != KjObject)
    return;

  bool anyChange = false;

  //
  // First pass: handle top-level keywords (type, scope). id is not
  // copied over — entity id is immutable on append.
  //
  for (KjNode* fP = fragment->value.firstChildP; fP != NULL; fP = fP->next)
  {
    if (fP->name == NULL)
      continue;
    if (strcmp(fP->name, "type") == 0)
    {
      applyType(target, fP, targetAllocP);
      anyChange = true;
    }
    else if (strcmp(fP->name, LD_VOCAB_SCOPE) == 0)
    {
      applyScope(target, fP, overwriteScope, targetAllocP);
      anyChange = true;
    }
  }

  //
  // Second pass: top-level attributes (anything not a keyword).
  //
  for (KjNode* fAttrP = fragment->value.firstChildP; fAttrP != NULL; fAttrP = fAttrP->next)
  {
    if (isEntityKeyword(fAttrP->name)) continue;
    if (strcmp(fAttrP->name, "type")         == 0) continue;
    if (strcmp(fAttrP->name, LD_VOCAB_SCOPE) == 0) continue;
    if (fAttrP->type != KjObject)                  continue;

    KjNode* tAttrP = kjLookup(target, fAttrP->name);

    if (tAttrP == NULL)
    {
      //
      // New attribute — clone the wrapper with all its dsKey instances.
      // Stamp createdAt/modifiedAt on every instance.
      //
      KjNode* clone = kjClone(targetAllocP, fAttrP);
      for (KjNode* instP = clone->value.firstChildP; instP != NULL; instP = instP->next)
      {
        if (instP->type != KjObject)
          continue;
        stampCreatedAtIfMissing(instP, ts, targetAllocP);
        bumpModifiedAt(instP, ts, targetAllocP);
      }
      kjChildAdd(target, clone);

      addReportEntry(reportP, fAttrP->name, "attributeCreated", NULL);
      anyChange = true;
      continue;
    }

    //
    // Existing attribute — set or append per dsKey instance.
    //
    KjNode* preClone = NULL;
    if (reportP != NULL)
      preClone = kjClone(swRest.kjsonP, tAttrP);

    for (KjNode* fInstP = fAttrP->value.firstChildP; fInstP != NULL; fInstP = fInstP->next)
    {
      if (fInstP->type != KjObject)
        continue;

      KjNode* tInstP = kjLookup(tAttrP, fInstP->name);

      if (tInstP == NULL)
      {
        // Add this dsKey instance
        KjNode* clone = kjClone(targetAllocP, fInstP);
        stampCreatedAtIfMissing(clone, ts, targetAllocP);
        bumpModifiedAt(clone, ts, targetAllocP);
        kjChildAdd(tAttrP, clone);
      }
      else
      {
        // Replace instance preserving createdAt
        KjNode* oldCreatedAt = kjLookup(tInstP, LD_VOCAB_CREATED_AT);
        long long createdAtNs = (oldCreatedAt != NULL && oldCreatedAt->type == KjInt)
                                ? oldCreatedAt->value.i
                                : (long long) ts;

        KjNode* newInst = kjClone(targetAllocP, fInstP);
        // Make sure newInst has createdAt (from old) and modifiedAt=ts
        KjNode* nCreated = kjLookup(newInst, LD_VOCAB_CREATED_AT);
        if (nCreated != NULL)
        {
          nCreated->type    = KjInt;
          nCreated->value.i = createdAtNs;
        }
        else
        {
          kjChildAdd(newInst, kjInteger(targetAllocP, LD_VOCAB_CREATED_AT, createdAtNs));
        }
        bumpModifiedAt(newInst, ts, targetAllocP);

        // Swap: remove old, add new (keeps the name key)
        kjChildRemove(tAttrP, tInstP);
        kjChildAdd(tAttrP, newInst);
      }
    }

    // The attr wrapper (dsKey-keyed map) holds no timestamps — only
    // individual instances do, which were stamped above.
    addReportEntry(reportP, fAttrP->name, "attributeModified", preClone);
    anyChange = true;
  }

  if (anyChange)
    bumpModifiedAt(target, ts, targetAllocP);
}
