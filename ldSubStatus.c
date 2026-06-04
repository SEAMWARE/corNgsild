//
// FILE            ldSubStatus.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stddef.h>                                      // NULL
#include <string.h>                                      // strcmp

#include "swNgsild/LdSubStatus.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// ldSubStatusFromString - unknown/NULL maps to active (the § 5.2.12 default)
//
LdSubStatus ldSubStatusFromString(const char* s)
{
  if (s == NULL)                   return LdSubStatusActive;
  if (strcmp(s, "active")  == 0)   return LdSubStatusActive;
  if (strcmp(s, "paused")  == 0)   return LdSubStatusPaused;
  if (strcmp(s, "expired") == 0)   return LdSubStatusExpired;
  if (strcmp(s, "failed")  == 0)   return LdSubStatusFailed;

  return LdSubStatusActive;
}



// -----------------------------------------------------------------------------
//
// ldSubStatusToString -
//
const char* ldSubStatusToString(LdSubStatus status)
{
  switch (status)
  {
    case LdSubStatusActive:   return "active";
    case LdSubStatusPaused:   return "paused";
    case LdSubStatusExpired:  return "expired";
    case LdSubStatusFailed:   return "failed";
  }

  return "active";
}
