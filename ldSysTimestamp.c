// -----------------------------------------------------------------------------
//
// FILE            ldSysTimestamp.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdio.h>                                       // snprintf
#include <string.h>                                      // strcmp, strcpy, strlen
#include <time.h>                                        // gmtime_r, strftime

#include "swRest/swRest.h"                            // swRest
#include "kalloc/KAlloc.h"                             // KAlloc
#include "kalloc/kaAlloc.h"                            // kaAlloc
#include "kjson/KjNode.h"                               // KjNode
#include "kjson/kjBuilder.h"                              // kjInteger, kjChildAdd
#include "kjson/kjLookup.h"                              // kjLookup
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_CREATED_AT, LD_VOCAB_MODIFIED_AT
#include "swNgsild/ldSysTimestamp.h"                     // Own interface



// -----------------------------------------------------------------------------
//
// ldSysTimestampToIso - epoch nanoseconds → ISO 8601 string
//
// Output: "2026-04-01T12:00:00Z" (whole second) or with a trimmed fraction
// "2026-04-01T12:00:00.123Z". Mirrors the entity sysattr rendering.
//
void ldSysTimestampToIso(long long nsec, char* buf, int bufSize)
{
  time_t     sec  = (time_t)(nsec / 1000000000LL);
  int        frac = (int)(nsec % 1000000000LL);
  struct tm  tm;

  gmtime_r(&sec, &tm);
  int n = strftime(buf, bufSize, "%Y-%m-%dT%H:%M:%S", &tm);

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
void ldSysTimestampsToIso(KjNode* treeP, KAlloc* allocP)
{
  if (treeP == NULL || treeP->type != KjObject)
    return;

  for (KjNode* childP = treeP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (childP->type == KjInt &&
        (strcmp(childP->name, LD_VOCAB_CREATED_AT)  == 0 ||
         strcmp(childP->name, LD_VOCAB_MODIFIED_AT) == 0))
    {
      char  isoBuf[32];
      ldSysTimestampToIso(childP->value.i, isoBuf, sizeof(isoBuf));

      char* isoStr = (char*) kaAlloc(allocP, 32);
      if (isoStr != NULL)
      {
        strcpy(isoStr, isoBuf);
        childP->type    = KjString;
        childP->value.s = isoStr;
      }
    }
  }
}



// -----------------------------------------------------------------------------
//
// ldSysTimestampCreate - stamp createdAt AND modifiedAt = request time
//
void ldSysTimestampCreate(KjNode* treeP)
{
  if (treeP == NULL || treeP->type != KjObject)
    return;

  long long now = (long long) swRest.requestStartTime;

  kjChildAdd(treeP, kjInteger(swRest.kjsonP, LD_VOCAB_CREATED_AT,  now));
  kjChildAdd(treeP, kjInteger(swRest.kjsonP, LD_VOCAB_MODIFIED_AT, now));
}



// -----------------------------------------------------------------------------
//
// ldSysTimestampModify - set/replace modifiedAt = request time
//
void ldSysTimestampModify(KjNode* treeP)
{
  if (treeP == NULL || treeP->type != KjObject)
    return;

  long long now  = (long long) swRest.requestStartTime;
  KjNode*   modP = kjLookup(treeP, LD_VOCAB_MODIFIED_AT);

  if (modP != NULL)
  {
    modP->type    = KjInt;
    modP->value.i = now;
  }
  else
    kjChildAdd(treeP, kjInteger(swRest.kjsonP, LD_VOCAB_MODIFIED_AT, now));
}
