//
// FILE            ldLangReduce.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
// Operates on COMPACTED trees (after swldCompactTree), so all names are
// short-form: "languageMap", "value", "type", "lang", "observedAt", etc.
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strcmp
#include "swRest/swRest.h"                            // swRest

#include "kalloc/KAlloc.h"                             // KAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjBuilder.h"                             // kjString
#include "kjson/kjChildReplace.h"                       // kjChildReplace

#include "swNgsild/ldLangReduce.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// isEntityKeyword - (compacted names)
//
static bool isEntityKeyword(const char* name)
{
  if (strcmp(name, "id")         == 0)  return true;
  if (strcmp(name, "type")       == 0)  return true;
  if (strcmp(name, "@context")   == 0)  return true;
  if (strcmp(name, "scope")      == 0)  return true;
  if (strcmp(name, "createdAt")  == 0)  return true;
  if (strcmp(name, "modifiedAt") == 0)  return true;

  return false;
}



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
// 2. Look up matching key: exact match > @none > first key
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
    // Find the best matching language key
    KjNode* matchP = NULL;
    KjNode* noneP  = NULL;
    KjNode* firstP = langMapP->value.firstChildP;

    for (KjNode* keyP = langMapP->value.firstChildP; keyP != NULL; keyP = keyP->next)
    {
      if (strcmp(keyP->name, lang) == 0)
      {
        matchP = keyP;
        break;
      }
      if (strcmp(keyP->name, "@none") == 0)
        noneP = keyP;
    }

    if (matchP == NULL)
      matchP = (noneP != NULL) ? noneP : firstP;

    if (matchP != NULL)
    {
      const char* chosenLang = matchP->name;

      // Create "value" node with the matched value
      KjNode* valueP = kjString(swRest.kjsonP, "value", matchP->value.s);
      valueP->type = matchP->type;
      if (matchP->type != KjString)
        valueP->value = matchP->value;

      // Replace "languageMap" with "value"
      kjChildReplace(attrP, langMapP, valueP);

      // Change type from "LanguageProperty" to "Property"
      if (typeP != NULL && typeP->type == KjString)
        typeP->value.s = (char*) "Property";

      // Add "lang" sub-property with the chosen language tag
      KjNode* langNodeP = kjString(swRest.kjsonP, "lang", chosenLang);
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
void ldLangReduce(KjNode* entityP, const char* lang, KAlloc* faP)
{
  if (entityP == NULL || entityP->type != KjObject)
    return;

  for (KjNode* childP = entityP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (childP->name != NULL && isEntityKeyword(childP->name) == false)
      attrLangReduce(childP, lang, faP);
  }
}
