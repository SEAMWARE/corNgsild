//
// FILE            ldLangReduce.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
// Operates on COMPACTED trees (after corLdCompactTree), so all names are
// short-form: "languageMap", "value", "type", "lang", "observedAt", etc.
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strcmp
#include "corRest/corRest.h"                            // corRest

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjBuilder.h"                             // kjString
#include "kjson/kjChildReplace.h"                       // kjChildReplace

#include "corNgsild/ldIsEntityKeyword.h"                   // ldIsEntityKeyword
#include "corNgsild/ldLangReduce.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// isAttrKeyword - (compacted names)
//
static bool isAttrKeyword(const char* name)
{
  if (strcmp(name, "type")        == 0)  return true;
  if (strcmp(name, "value")       == 0)  return true;
  if (strcmp(name, "object")      == 0)  return true;
  if (strcmp(name, "languageMap") == 0)  return true;
  if (strcmp(name, "vocab")       == 0)  return true;
  if (strcmp(name, "valueList")   == 0)  return true;
  if (strcmp(name, "objectList")  == 0)  return true;
  if (strcmp(name, "json")        == 0)  return true;
  if (strcmp(name, "observedAt")  == 0)  return true;
  if (strcmp(name, "unitCode")    == 0)  return true;
  if (strcmp(name, "datasetId")   == 0)  return true;
  if (strcmp(name, "lang")        == 0)  return true;

  return false;
}



// -----------------------------------------------------------------------------
//
// attrLangReduce - reduce a single LanguageProperty attribute to Property
//
// 1. Find the "languageMap" child
// 2. Look up matching key: exact match > @none > "en" > first key
// 3. Change "type" from "LanguageProperty" to "Property"
// 4. Replace "languageMap" with "value" (the matched string)
// 5. Add a "lang" sub-property with the chosen language tag
// 6. Recurse into sub-attributes
//
static void attrLangReduce(KjNode* attrP, const char* lang, KAlloc* faP)
{
  if (attrP->type != KjObject)
    return;

  // Find languageMap child — if not found, this is not a LanguageProperty
  KjNode* langMapP = NULL;
  KjNode* typeP    = NULL;

  for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, "languageMap") == 0)
      langMapP = childP;
    else if (strcmp(childP->name, "type") == 0)
      typeP = childP;
  }

  if (langMapP != NULL && langMapP->type == KjObject)
  {
    // Find the best matching language key. Fallback order when the requested
    // language is absent (clause 10): @none if present (spec), then "en" as our
    // implementation-defined default for the "up to the implementation" choice,
    // then the first key.
    KjNode* matchP = NULL;
    KjNode* noneP  = NULL;
    KjNode* enP    = NULL;
    KjNode* firstP = langMapP->value.firstChildP;

    for (KjNode* keyP = langMapP->value.firstChildP; keyP != NULL; keyP = keyP->next)
    {
      if (strcmp(keyP->name, lang) == 0)
      {
        matchP = keyP;
        break;
      }
      if      (strcmp(keyP->name, "@none") == 0)  noneP = keyP;
      else if (strcmp(keyP->name, "en")    == 0)  enP   = keyP;
    }

    if (matchP == NULL)
      matchP = (noneP != NULL) ? noneP : (enP != NULL) ? enP : firstP;

    if (matchP != NULL)
    {
      const char* chosenLang = matchP->name;

      // Create "value" node with the matched value
      KjNode* valueP = kjString(corRest.kjsonP, "value", matchP->value.s);
      valueP->type = matchP->type;
      if (matchP->type != KjString)
        valueP->value = matchP->value;

      // Replace "languageMap" with "value"
      kjChildReplace(attrP, langMapP, valueP);

      // Change type from "LanguageProperty" to "Property"
      if (typeP != NULL && typeP->type == KjString)
        typeP->value.s = (char*) "Property";

      // Add "lang" sub-property with the chosen language tag
      KjNode* langNodeP = kjString(corRest.kjsonP, "lang", chosenLang);
      kjChildAdd(attrP, langNodeP);
    }
  }

  // Recurse into sub-attributes
  for (KjNode* childP = attrP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (isAttrKeyword(childP->name) == false)
      attrLangReduce(childP, lang, faP);
  }
}



// -----------------------------------------------------------------------------
//
// ldLangReduce - reduce LanguageProperty attributes to Property with matching language
//
// For the temporal API (§ 5.7.3) an attribute is rendered as an array of
// instance objects rather than a single object. When the entity-level child
// is an array, walk it and reduce each instance.
//
void ldLangReduce(KjNode* entityP, const char* lang, KAlloc* faP)
{
  if (entityP == NULL || entityP->type != KjObject)
    return;

  for (KjNode* childP = entityP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (childP->name == NULL || ldIsEntityKeyword(childP->name))
      continue;

    if (childP->type == KjArray)
    {
      for (KjNode* instP = childP->value.firstChildP; instP != NULL; instP = instP->next)
        attrLangReduce(instP, lang, faP);
    }
    else
    {
      attrLangReduce(childP, lang, faP);
    }
  }
}
