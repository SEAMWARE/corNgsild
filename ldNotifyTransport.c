//
// FILE            ldNotifyTransport.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Notification delivery to anything that is not HTTP - see header.
//
#include <stdbool.h>                                     // bool
#include <string.h>                                      // strncmp, strlen

#include "corBase/corLibLog.h"                           // COR_LIB_W
#include "corAlloc/corAlloc.h"                           // corAlloc
#include "corTree/CorNode.h"                             // CorNode
#include "corTree/corTreeBuilder.h"                      // corTreeObject, corTreeString, corTreeChildAdd
#include "corTree/corTreeLookup.h"                       // corTreeLookup
#include "corJson/corJsonParse.h"                        // corJsonParse
#include "corJson/corJsonRender.h"                       // corJsonFastRender
#include "corJson/corJsonRenderSize.h"                   // corJsonFastRenderSize

#include "corRest/corRest.h"                             // corRest

#include "corNgsild/ldTermId.h"                          // ldNodeRename
#include "corNgsild/ldNotifyTransport.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// The application's hooks - set once at startup, read by every notifying thread after that
//
static LdNotifyTransportHas  transportHas  = NULL;
static LdNotifyTransportSend transportSend = NULL;



// -----------------------------------------------------------------------------
//
// ldNotifyTransportSet -
//
void ldNotifyTransportSet(LdNotifyTransportHas hasFn, LdNotifyTransportSend sendFn)
{
  transportHas  = hasFn;
  transportSend = sendFn;
}



// -----------------------------------------------------------------------------
//
// ldNotifyIsHttp -
//
bool ldNotifyIsHttp(const char* uri)
{
  if (uri == NULL)
    return false;

  return (strncmp(uri, "http://", 7) == 0) || (strncmp(uri, "https://", 8) == 0);
}



// -----------------------------------------------------------------------------
//
// ldNotifyTransportHas -
//
bool ldNotifyTransportHas(const char* uri)
{
  if ((uri == NULL) || (transportHas == NULL))
    return false;

  return transportHas(uri);
}



// -----------------------------------------------------------------------------
//
// envelope - the TS 104 243 message: { "metadata": {...}, "body": <the Notification> }
//
// Rendered into corRest.kalloc; NULL if the Notification does not parse back.
//
static char* envelope(const char* notifBodyJson, const char* contentType, const char* linkHeader, CorNode* receiverInfo)
{
  CorNode* root     = corTreeObject(corRest.kallocP, NULL);
  CorNode* metadata = corTreeObject(corRest.kallocP, "metadata");

  if (contentType != NULL)
    corTreeChildAdd(metadata, corTreeString(corRest.kallocP, "Content-Type", (char*) contentType));

  if (linkHeader != NULL)
    corTreeChildAdd(metadata, corTreeString(corRest.kallocP, "Link", (char*) linkHeader));

  //
  // receiverInfo - what the subscriber asked to receive with every message: in HTTP, headers;
  // here, members of metadata
  //
  if ((receiverInfo != NULL) && (receiverInfo->type == CorArray))
  {
    for (CorNode* kvP = receiverInfo->value.head; kvP != NULL; kvP = kvP->next)
    {
      if (kvP->type != CorObject)
        continue;

      CorNode* kP = corTreeLookup(kvP, "key");
      CorNode* vP = corTreeLookup(kvP, "value");

      if ((kP == NULL) || (kP->type != CorString) || (vP == NULL) || (vP->type != CorString))
        continue;

      corTreeChildAdd(metadata, corTreeString(corRest.kallocP, kP->value.s, vP->value.s));
    }
  }

  corTreeChildAdd(root, metadata);

  CorNode* bodyTree = corJsonParse(corRest.corJsonP, (char*) notifBodyJson);
  if (bodyTree == NULL)
    return NULL;

  ldNodeRename(bodyTree, (char*) "body");
  corTreeChildAdd(root, bodyTree);

  char* buf = (char*) corAlloc(&corRest.kalloc, corJsonFastRenderSize(root) + 1);
  corJsonFastRender(root, buf);

  return buf;
}



// -----------------------------------------------------------------------------
//
// ldNotifyTransportSend -
//
bool ldNotifyTransportSend(const char* uri,
                           const char* notifBodyJson,
                           const char* contentType,
                           const char* linkHeader,
                           CorNode*    receiverInfo,
                           CorNode*    notifierInfo)
{
  if (transportSend == NULL)
  {
    COR_LIB_W("notification to '%s': no transport for its scheme", uri);
    return false;
  }

  char* payload = envelope(notifBodyJson, contentType, linkHeader, receiverInfo);
  if (payload == NULL)
    return false;

  char* infoJson = NULL;
  if ((notifierInfo != NULL) && (notifierInfo->type == CorArray))
  {
    //
    // Rendered as an array of its own, not as the member it is: the transport is handed the VALUE
    //
    CorNode infoArray = *notifierInfo;

    infoArray.name = NULL;
    infoArray.next = NULL;
    infoJson       = (char*) corAlloc(&corRest.kalloc, corJsonFastRenderSize(&infoArray) + 1);
    corJsonFastRender(&infoArray, infoJson);
  }

  return transportSend(uri, payload, infoJson);
}
