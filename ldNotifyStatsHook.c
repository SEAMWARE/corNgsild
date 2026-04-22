//
// FILE            ldNotifyStatsHook.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stddef.h>                                 // NULL

#include "swNgsild/ldNotifyStatsHook.h"             // Own interface



static LdNotifyStatsHook hook = NULL;



void ldNotifyStatsHookSet(LdNotifyStatsHook fn)
{
  hook = fn;
}



void ldNotifyStatsHookInvoke(bool csrSub, bool success)
{
  if (hook != NULL)
    hook(csrSub, success);
}
