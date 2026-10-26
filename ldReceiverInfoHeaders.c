//
// FILE            ldReceiverInfoHeaders.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strcmp, strstr
#include <strings.h>                                     // strcasecmp

#include "corTree/CorNode.h"                             // CorNode
#include "corTree/corTreeLookup.h"                       // corTreeLookup
#include "corRest/corRestClient.h"                       // CorRestClientRequest, corRestClientRequestHeader

#include "corNgsild/ldRequestSubstitute.h"               // ldRequestSubstitute
#include "corNgsild/ldReceiverInfoHeaders.h"             // Own interface



// -----------------------------------------------------------------------------
//
// brokerEmitted - a header the broker emits itself, never taken from receiverInfo
//
static bool brokerEmitted(const char* key, const char* value)
{
  if ((strcasecmp(key, "Content-Type")    == 0) ||
      (strcasecmp(key, "Prefer")          == 0) ||
      (strcasecmp(key, "NGSILD-Tenant")   == 0) ||
      (strcasecmp(key, "NGSILD-Snapshot") == 0))
    return true;

  return (strcasecmp(key, "Link") == 0) && (strstr(value, "json-ld#context") != NULL);
}



// -----------------------------------------------------------------------------
//
// ldReceiverInfoHeadersAdd -
//
void ldReceiverInfoHeadersAdd(CorRestClientRequest* reqP, CorNode* receiverInfo, bool fromRequest)
{
  if ((receiverInfo == NULL) || (receiverInfo->type != CorArray))
    return;

  for (CorNode* kvP = receiverInfo->value.head; kvP != NULL; kvP = kvP->next)
  {
    if (kvP->type != CorObject)
      continue;

    CorNode* kP = corTreeLookup(kvP, "key");
    CorNode* vP = corTreeLookup(kvP, "value");

    if ((kP == NULL) || (kP->type != CorString) || (vP == NULL) || (vP->type != CorString))
      continue;

    if (brokerEmitted(kP->value.s, vP->value.s) == true)
      continue;

    const char* hv;

    if (fromRequest == true)
      hv = ldRequestSubstitute(kP->value.s, vP->value.s);
    else
      hv = (strcmp(vP->value.s, "urn:ngsi-ld:request") == 0) ? NULL : vP->value.s;

    if (hv != NULL)
      corRestClientRequestHeader(reqP, kP->value.s, hv);
  }
}
