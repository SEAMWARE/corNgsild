//
// FILE            ldQAttrs.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Collect attribute names from an LdQNode tree (see ldQAttrs.h).
//
#include <string.h>                                    // strcmp

#include "kalloc/kaAlloc.h"                            // kaAlloc

#include "corNgsild/LdQ.h"                              // LdQNode, LdQTerm
#include "corNgsild/ldQAttrs.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// addUnique - append name to outV[], skipping duplicates
//
static void addUnique(const char* name, const char** outV, int* outNP, int cap)
{
  if (name == NULL)
    return;

  for (int i = 0; i < *outNP; i++)
  {
    if (strcmp(outV[i], name) == 0)
      return;
  }

  if (*outNP < cap)
    outV[(*outNP)++] = name;
}



// -----------------------------------------------------------------------------
//
// countAttrs - upper bound on the number of distinct attrs (with dupes)
//
static int countAttrs(LdQNode* nodeP)
{
  if (nodeP == NULL)
    return 0;

  if (nodeP->type == LdQTermNode)
    return 1;

  if (nodeP->type == LdQLinkedNode)
    return 1 + countAttrs(nodeP->linked.subQ);

  if (nodeP->type == LdQAndNode || nodeP->type == LdQOrNode)
  {
    int sum = 0;
    for (int i = 0; i < nodeP->group.count; i++)
      sum += countAttrs(nodeP->group.childV[i]);
    return sum;
  }

  return 0;
}



// -----------------------------------------------------------------------------
//
// walk - depth-first add of every attribute name
//
static void walk(LdQNode* nodeP, const char** outV, int* outNP, int cap)
{
  if (nodeP == NULL)
    return;

  if (nodeP->type == LdQTermNode)
  {
    addUnique(nodeP->term.attr, outV, outNP, cap);
    return;
  }

  if (nodeP->type == LdQLinkedNode)
  {
    addUnique(nodeP->linked.relName, outV, outNP, cap);
    walk(nodeP->linked.subQ, outV, outNP, cap);
    return;
  }

  if (nodeP->type == LdQAndNode || nodeP->type == LdQOrNode)
  {
    for (int i = 0; i < nodeP->group.count; i++)
      walk(nodeP->group.childV[i], outV, outNP, cap);
  }
}



//
// ldQAttrs -
//
char** ldQAttrs(LdQNode* nodeP, KAlloc* kaP)
{
  if (nodeP == NULL)
    return NULL;

  int cap = countAttrs(nodeP);
  if (cap == 0)
    return NULL;

  // +1 for the NULL terminator
  const char** outV = (const char**) kaAlloc(kaP, (cap + 1) * sizeof(char*));
  int          outN = 0;

  walk(nodeP, outV, &outN, cap);

  if (outN == 0)
    return NULL;

  outV[outN] = NULL;
  return (char**) outV;
}
