//
// FILE            ldLangReduce.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
// Operates on COMPACTED trees (after corLdCompactTree) - except for the
// languageMap keys, compared as tags either way (ldLanguageTag) - so names are
// short-form: "languageMap", "value", "type", "lang", "observedAt", etc.
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strcmp
#include "corRest/corRest.h"                            // corRest

#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corTree/CorNode.h"                            // CorNode
#include "corTree/corTreeBuilder.h"                      // corTreeString
#include "corTree/corTreeChildReplace.h"                // corTreeChildReplace

#include "corNgsild/CorNgsild.h"                     // corNgsild
#include "corNgsild/ldIsEntityKeyword.h"                   // ldIsEntityKeyword
#include "corNgsild/ldLanguageKey.h"                       // ldLanguageTag
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
static void attrLangReduce(CorNode* attrP, const char* lang, CorAlloc* faP)
{
  if (attrP->type != CorObject)
    return;

  // Find languageMap child — if not found, this is not a LanguageProperty
  CorNode* langMapP = NULL;
  CorNode* typeP   = NULL;

  for (CorNode* childP = attrP->value.head; childP != NULL; childP = childP->next)
  {
    if (strcmp(childP->name, "languageMap") == 0)
      langMapP = childP;
    else if (strcmp(childP->name, "type") == 0)
      typeP = childP;
  }

  if (langMapP != NULL && langMapP->type == CorObject)
  {
    // Find the best matching language key. Fallback order when the requested
    // language is absent (clause 10): @none if present (spec), then "en" as our
    // implementation-defined default for the "up to the implementation" choice,
    // then the first key.
    CorNode* matchP = NULL;
    CorNode* noneP = NULL;
    CorNode* enP   = NULL;
    CorNode* firstP = langMapP->value.head;

    //
    // Keys are compared as TAGS: on a tree that has not been compacted yet they are
    // still the broker's expanded encoding (ldLanguageKey.h); on a compacted one
    // ldLanguageTag returns them as they are.
    //
    for (CorNode* keyP = langMapP->value.head; keyP != NULL; keyP = keyP->next)
    {
      const char* tag = ldLanguageTag(keyP->name, corNgsild.contextP);

      if (strcmp(tag, lang) == 0)
      {
        matchP = keyP;
        break;
      }
      if      (strcmp(tag, "@none") == 0)  noneP = keyP;
      else if (strcmp(tag, "en")    == 0)  enP   = keyP;
    }

    if (matchP == NULL)
      matchP = (noneP != NULL) ? noneP : (enP != NULL) ? enP : firstP;

    if (matchP != NULL)
    {
      const char* chosenLang = ldLanguageTag(matchP->name, corNgsild.contextP);

      // Create "value" node with the matched value
      CorNode* valueP = corTreeString(corRest.kallocP, "value", matchP->value.s);
      valueP->type = matchP->type;
      if (matchP->type != CorString)
        valueP->value = matchP->value;

      // Replace "languageMap" with "value"
      corTreeChildReplace(attrP, langMapP, valueP);

      // Change type from "LanguageProperty" to "Property"
      if (typeP != NULL && typeP->type == CorString)
        typeP->value.s = (char*) "Property";

      // Add "lang" sub-property with the chosen language tag
      CorNode* langNodeP = corTreeString(corRest.kallocP, "lang", chosenLang);
      corTreeChildAdd(attrP, langNodeP);
    }
  }

  // Recurse into sub-attributes
  for (CorNode* childP = attrP->value.head; childP != NULL; childP = childP->next)
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
void ldLangReduce(CorNode* entityP, const char* lang, CorAlloc* faP)
{
  if (entityP == NULL || entityP->type != CorObject)
    return;

  for (CorNode* childP = entityP->value.head; childP != NULL; childP = childP->next)
  {
    if (childP->name == NULL || ldIsEntityKeyword(childP->name))
      continue;

    if (childP->type == CorArray)
    {
      for (CorNode* instP = childP->value.head; instP != NULL; instP = instP->next)
        attrLangReduce(instP, lang, faP);
    }
    else
    {
      attrLangReduce(childP, lang, faP);
    }
  }
}
