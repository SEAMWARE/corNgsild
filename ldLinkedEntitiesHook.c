//
// FILE            ldLinkedEntitiesHook.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stddef.h>                                     // NULL

#include "corNgsild/ldLinkedEntitiesHook.h"               // Own interface



static LdLinkedEntitiesExpandHook hook = NULL;



void ldLinkedEntitiesHookSet(LdLinkedEntitiesExpandHook fn)
{
  hook = fn;
}



void ldLinkedEntitiesHookInvoke(KjNode* dataArrayP, const char* mode, int joinLevel, bool sysAttrs, void* tenantP)
{
  if (hook != NULL && dataArrayP != NULL && mode != NULL && tenantP != NULL)
    hook(dataArrayP, mode, joinLevel, sysAttrs, tenantP);
}
