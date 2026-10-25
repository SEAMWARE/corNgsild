//
// FILE            ldUrlPathShown.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <string.h>                                    // strchr

#include "corBase/corUriPasswordMask.h"                // corUriPasswordMask

#include "corNgsild/ldUrlPathShown.h"                  // Own interface



// -----------------------------------------------------------------------------
//
// ldUrlPathShown -
//
char* ldUrlPathShown(const char* url, char* buf, int bufSize)
{
  corUriPasswordMask(url, buf, bufSize);

  char* q = strchr(buf, '?');

  if (q != NULL)
    *q = 0;

  return buf;
}
