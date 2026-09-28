//
// FILE            ldParse.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                     // bool

#include "corTree/CorNode.h"                            // CorNode
#include "corJson/CorJson.h"                            // CorJson
#include "corRest/corRest.h"                            // corRest
#include "corJsonld/CorLdItem.h"                        // CorLdItem
#include "corJsonld/corLdCoreLookup.h"                  // corLdCoreLookup

#include "corNgsild/ldTermId.h"                         // LD_TERM_NOT_CORE
#include "corNgsild/ldParse.h"                          // Own interface



// -----------------------------------------------------------------------------
//
// ldParseKeyHook -
//
bool ldParseKeyHook(CorJson* corJsonP, CorNode* containerP, CorNode* nodeP, int depth)
{
  (void) corJsonP;
  (void) containerP;
  (void) depth;

  CorLdItem* itemP = corLdCoreLookup(nodeP->name);

  nodeP->termId = ((itemP != NULL) && (itemP->termId != 0)) ? itemP->termId : LD_TERM_NOT_CORE;

  return true;
}



// -----------------------------------------------------------------------------
//
// ldPrePayloadParseHook -
//
void ldPrePayloadParseHook(void)
{
  bool ngsiLd = (corRest.serviceP != NULL) && (corRest.serviceP->ldOp != 0);

  corRest.corJsonP->keyF = (ngsiLd == true) ? ldParseKeyHook : NULL;
}
