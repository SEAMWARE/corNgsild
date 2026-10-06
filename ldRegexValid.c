//
// FILE            ldRegexValid.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                  // bool
#include <stdio.h>                                    // snprintf
#include <ctype.h>                                    // isdigit
#include <regex.h>                                    // regcomp, regerror, regfree

#include "corNgsild/ldRegexValid.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// intervalAt - does an interval ({n}, {n,}, {n,m}) start at 'p'?
//
static bool intervalAt(const char* p)
{
  return (p[0] == '{') && (isdigit((unsigned char) p[1]) != 0);
}



// -----------------------------------------------------------------------------
//
// ldRegexValid -
//
bool ldRegexValid(const char* pattern, char* errBuf, int errBufLen)
{
  //
  // The duplication symbols: one right after another, or with nothing before it to repeat
  //
  bool canRepeat = false;                             // the last thing read is something a duplication symbol can apply to
  bool afterDup  = false;                             // the last thing read is a duplication symbol

  for (const char* p = pattern; *p != 0; p++)
  {
    bool dup = (*p == '*') || (*p == '+') || (*p == '?') || intervalAt(p);

    if (dup)
    {
      if (afterDup || (canRepeat == false))
      {
        snprintf(errBuf, errBufLen, "a duplication symbol at offset %d %s - undefined in a POSIX extended regular expression",
                 (int) (p - pattern), afterDup ? "follows another one" : "has nothing to repeat");
        return false;
      }

      if (*p == '{')
      {
        while ((*p != 0) && (*p != '}'))
          p++;
        if (*p == 0)
          break;                                      // regcomp says it
      }

      afterDup = true;
      continue;
    }

    afterDup = false;

    if (*p == '\\')
    {
      if (p[1] != 0)
        p++;
      canRepeat = true;
    }
    else if (*p == '[')                               // a bracket expression, to its closing ']' ('[]...]', '[^]...]', '[:class:]' inside)
    {
      p++;
      if (*p == '^')
        p++;
      if (*p == ']')
        p++;

      while ((*p != 0) && (*p != ']'))
      {
        if ((*p == '[') && ((p[1] == ':') || (p[1] == '.') || (p[1] == '=')))
        {
          char close = p[1];
          p += 2;
          while ((*p != 0) && !((*p == close) && (p[1] == ']')))
            p++;
          if (*p != 0)
            p++;
        }
        if (*p != 0)
          p++;
      }

      if (*p == 0)
        break;                                        // regcomp says it
      canRepeat = true;
    }
    else if ((*p == '(') || (*p == '|'))
      canRepeat = false;
    else
      canRepeat = true;                               // a character, '.', ')', or an anchor
  }

  regex_t re;
  int     rc = regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB);

  if (rc != 0)
  {
    regerror(rc, &re, errBuf, errBufLen);
    return false;
  }

  regfree(&re);
  return true;
}
