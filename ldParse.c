//
// FILE            ldParse.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // uint16_t
#include <string.h>                                      // strcmp

#include "corAlloc/corAlloc.h"                          // corAlloc
#include "corTree/CorNode.h"                            // CorNode
#include "corJson/CorJson.h"                            // CorJson
#include "corRest/corRest.h"                            // corRest
#include "corJsonld/CorLdItem.h"                        // CorLdItem
#include "corJsonld/corLdCoreLookup.h"                  // corLdCoreLookup
#include "corJsonld/corLdInit.h"                        // corLdCoreContext
#include "corJsonld/corLdCompact.h"                     // corLdCompact

#include "corNgsild/CorNgsild.h"                        // corNgsild
#include "corNgsild/CorTerm.h"                          // CorTerm*
#include "corNgsild/LdProblem.h"                        // LD_ERROR_BAD_REQUEST_DATA
#include "corNgsild/ldError.h"                          // ldError
#include "corNgsild/ldTermId.h"                         // ldTermId, ldNodeRename, LD_TERM_NOT_CORE
#include "corNgsild/ldTermClass.h"                      // ldTermClass, LD_TC_VALUE_KEY
#include "corNgsild/ldParse.h"                          // Own interface



// -----------------------------------------------------------------------------
//
// ldParseKeyHook -
//
bool ldParseKeyHook(CorJson* corJsonP, CorNode* containerP, CorNode* nodeP, int depth)
{
  (void) corJsonP;

  CorLdItem* itemP = corLdCoreLookup(nodeP->name);
  CorTerm    term  = ((itemP != NULL) && (itemP->termId != 0)) ? (CorTerm) itemP->termId : CorTermNone;

  nodeP->termId = (term != CorTermNone) ? (uint16_t) term : LD_TERM_NOT_CORE;

  LdParseState* stateP = corNgsild.parseP;

  if ((stateP == NULL) || (depth >= LD_PARSE_DEPTH_MAX))
    return true;

  //
  // Climbing back: the levels below this one belonged to a branch that is finished
  //
  for (int k = depth + 1; k <= stateP->deepest; k++)
  {
    stateP->level[k].containerP = NULL;
    stateP->level[k].memberSeen = false;
  }
  stateP->deepest = depth;

  LdParseLevel* levelP = &stateP->level[depth];

  if (levelP->containerP != containerP)
  {
    //
    // A new container: it lies inside a value if the member it is the value of does - the
    // latest member at the nearest shallower level that has one (an array has none)
    //
    bool inValue = false;

    for (int k = depth - 1; k >= 1; k--)
    {
      if (stateP->level[k].memberSeen == true)
      {
        inValue = stateP->level[k].memberInValue;
        break;
      }
    }

    levelP->containerP = containerP;
    levelP->inValue    = inValue;
  }

  bool atMember = (nodeP->name[0] == '@');

  levelP->memberSeen    = true;
  levelP->memberInValue = (levelP->inValue == true) || (atMember == true) || ((ldTermClass[term] & LD_TC_VALUE_KEY) != 0);

  //
  // "@id"/"@type" in a structural position: renamed once the body is complete (the
  // container may yet turn out to be a JSON-LD value object)
  //
  if ((atMember == true) && (levelP->inValue == false) && ((term == CorTermId) || (term == CorTermType)))
  {
    if (stateP->aliasN == stateP->aliasMax)
    {
      int        max  = (stateP->aliasMax == 0) ? 8 : 2 * stateP->aliasMax;
      CorNode**  vP   = (CorNode**) corAlloc(&corRest.kalloc, 2 * max * sizeof(CorNode*));

      if (vP == NULL)
        return true;

      for (int i = 0; i < stateP->aliasN; i++)
      {
        vP[i]       = stateP->aliasV[i];
        vP[max + i] = stateP->aliasContainerV[i];
      }

      stateP->aliasV          = vP;
      stateP->aliasContainerV = &vP[max];
      stateP->aliasMax        = max;
    }

    stateP->aliasV[stateP->aliasN]          = nodeP;
    stateP->aliasContainerV[stateP->aliasN] = containerP;
    stateP->aliasN += 1;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// isValueObject - a JSON-LD value object: has an "@value" member
//
static bool isValueObject(CorNode* objectP)
{
  for (CorNode* childP = objectP->value.head; childP != NULL; childP = childP->next)
  {
    if ((childP->name != NULL) && (childP->name[0] == '@') && (strcmp(childP->name, "@value") == 0))
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// ldParseAliasesApply -
//
bool ldParseAliasesApply(void)
{
  LdParseState* stateP = corNgsild.parseP;

  if (stateP == NULL)
    return true;

  for (int i = 0; i < stateP->aliasN; i++)
  {
    CorNode* aliasP     = stateP->aliasV[i];
    CorNode* containerP = stateP->aliasContainerV[i];
    CorTerm  term       = ldTermId(aliasP);

    if (isValueObject(containerP) == true)
      continue;

    for (CorNode* childP = containerP->value.head; childP != NULL; childP = childP->next)
    {
      if ((childP != aliasP) && (childP->name != NULL) && (childP->name[0] != '@') && (ldTermId(childP) == term))
      {
        //
        // The error names what the client SENT - a container's name is expanded by now,
        // and the top-level one is corJson's "toplevel object"
        //
        const char* where = "the payload";

        if (containerP != corRest.in.requestTree)
        {
          where = ((containerP->name != NULL) && (containerP->name[0] != 0)) ? containerP->name : "an array item";

          if ((containerP->name != NULL) && (containerP->name[0] != 0))
          {
            const char* sentP = corLdCompact((corNgsild.contextP != NULL) ? corNgsild.contextP : corLdCoreContext(), containerP->name);

            if (sentP != NULL)
              where = sentP;
          }
        }

        bool        isId   = (term == CorTermId);
        bool        quoted = (containerP != corRest.in.requestTree);

        ldError(400, LD_ERROR_BAD_REQUEST_DATA, isId ? "Duplicate Id" : "Duplicate Type",
                "Duplicate '%s' / '%s' in %s%s%s", isId ? "id" : "type", isId ? "@id" : "@type",
                quoted ? "'" : "", where, quoted ? "'" : "");
        return false;
      }
    }

    ldNodeRename(aliasP, (term == CorTermId) ? "id" : "type");
    aliasP->termId = (uint16_t) term;             // the same term - keep the stamp
  }

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

  if (ngsiLd == true)
  {
    corNgsild.parseP = (LdParseState*) corAlloc(&corRest.kalloc, sizeof(LdParseState));   // the arena zeroes it

    if (corNgsild.parseP == NULL)
      corRest.corJsonP->keyF = NULL;      // no state: stamping-only would still be right, but keep it simple - plain parse
  }
}
