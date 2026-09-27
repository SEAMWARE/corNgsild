// -----------------------------------------------------------------------------
//
// FILE            ldSysTimestamp.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdio.h>                                       // snprintf
#include <string.h>                                      // strcmp, strcpy, strlen
#include <time.h>                                        // gmtime_r, strftime

#include "corRest/corRest.h"                            // corRest
#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corAlloc/corAlloc.h"                         // corAlloc
#include "corTree/CorNode.h"                            // CorNode
#include "corTree/corTreeBuilder.h"                       // corTreeInteger, corTreeChildAdd
#include "corTree/corTreeLookup.h"                       // corTreeLookup
#include "corNgsild/LdVocab.h"                            // LD_VOCAB_CREATED_AT, LD_VOCAB_MODIFIED_AT
#include "corNgsild/ldSysTimestamp.h"                     // Own interface



// -----------------------------------------------------------------------------
//
// ldSysTimestampToIso - epoch nanoseconds → ISO 8601 string
//
// Output: "2026-04-01T12:00:00Z" (whole second) or with a trimmed fraction
// "2026-04-01T12:00:00.123Z". Mirrors the entity sysattr rendering.
//
void ldSysTimestampToIso(long long nsec, char* buf, int bufSize)
{
  extern bool ldTimestampHighPrecision;  // §5.2.2.4: 6 fractional digits by default, 9 with -hp

  time_t     sec  = (time_t)(nsec / 1000000000LL);
  int        frac = (int)(nsec % 1000000000LL);
  struct tm  tm;

  gmtime_r(&sec, &tm);
  int n = strftime(buf, bufSize, "%Y-%m-%dT%H:%M:%S", &tm);

  // NGSI-LD (§ 5.2.2.4) caps DateTime at 6 fractional digits (microseconds). Drop the
  // sub-microsecond part so the trailing-zero trim below yields at most 6 digits; -hp keeps all 9.
  if (!ldTimestampHighPrecision)
    frac = (frac / 1000) * 1000;

  if (frac == 0)
  {
    buf[n++] = 'Z';
    buf[n]   = 0;
  }
  else
  {
    snprintf(buf + n, bufSize - n, ".%09d", frac);
    int end = strlen(buf) - 1;
    while (end > n && buf[end] == '0')
      end--;
    buf[end + 1] = 'Z';
    buf[end + 2] = 0;
  }
}



// -----------------------------------------------------------------------------
//
// ldSysTimestampsToIso - convert top-level createdAt/modifiedAt int → ISO string
//
// Subscriptions and Registrations carry these two system attributes only at the
// top level (unlike entities, whose attributes each carry their own), so a
// non-recursive pass is enough.
//
void ldSysTimestampsToIso(CorNode* treeP, CorAlloc* allocP)
{
  if (treeP == NULL || treeP->type != CorObject)
    return;

  for (CorNode* childP = treeP->value.head; childP != NULL; childP = childP->next)
  {
    if (childP->type == CorInt &&
        (strcmp(childP->name, LD_VOCAB_CREATED_AT)  == 0 ||
         strcmp(childP->name, LD_VOCAB_MODIFIED_AT) == 0))
    {
      char  isoBuf[32];
      ldSysTimestampToIso(childP->value.i, isoBuf, sizeof(isoBuf));

      char* isoStr = (char*) corAlloc(allocP, 32);
      if (isoStr != NULL)
      {
        strcpy(isoStr, isoBuf);
        childP->type    = CorString;
        childP->value.s = isoStr;
      }
    }
  }
}



// -----------------------------------------------------------------------------
//
// ldSysTimestampCreate - stamp createdAt AND modifiedAt = request time
//
void ldSysTimestampCreate(CorNode* treeP)
{
  if (treeP == NULL || treeP->type != CorObject)
    return;

  long long now = (long long) corRest.requestStartTime;

  corTreeChildAdd(treeP, corTreeInteger(corRest.kallocP, LD_VOCAB_CREATED_AT, now));
  corTreeChildAdd(treeP, corTreeInteger(corRest.kallocP, LD_VOCAB_MODIFIED_AT, now));
}



// -----------------------------------------------------------------------------
//
// ldSysTimestampModify - set/replace modifiedAt = request time
//
void ldSysTimestampModify(CorNode* treeP)
{
  if (treeP == NULL || treeP->type != CorObject)
    return;

  long long now  = (long long) corRest.requestStartTime;
  CorNode*  modP = corTreeLookup(treeP, LD_VOCAB_MODIFIED_AT);

  if (modP != NULL)
  {
    modP->type    = CorInt;
    modP->value.i = now;
  }
  else
    corTreeChildAdd(treeP, corTreeInteger(corRest.kallocP, LD_VOCAB_MODIFIED_AT, now));
}
