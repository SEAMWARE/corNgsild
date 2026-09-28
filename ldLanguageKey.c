//
// FILE            ldLanguageKey.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stddef.h>                                      // NULL

#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corJsonld/CorLdContext.h"                    // CorLdContext
#include "corJsonld/corLdInit.h"                        // corLdCoreContext
#include "corJsonld/corLdExpand.h"                      // corLdExpand
#include "corJsonld/corLdCompact.h"                     // corLdCompact

#include "corNgsild/ldLanguageKey.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// ldLanguageTag -
//
const char* ldLanguageTag(const char* key, CorLdContext* contextP)
{
  if ((key == NULL) || (key[0] == '@'))
    return key;

  const char* tagP = corLdCompact((contextP != NULL) ? contextP : corLdCoreContext(), key);

  return (tagP != NULL) ? tagP : key;
}



// -----------------------------------------------------------------------------
//
// ldLanguageKey -
//
const char* ldLanguageKey(const char* tag, CorLdContext* contextP, CorAlloc* kaP)
{
  if ((tag == NULL) || (tag[0] == '@'))
    return tag;

  return corLdExpand((contextP != NULL) ? contextP : corLdCoreContext(), tag, kaP, NULL, NULL);
}
