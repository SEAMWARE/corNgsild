//
// FILE            ldForwarding.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Forwarding-plugin registry. Implementation is intentionally trivial:
// a fixed-size linear array. Plugins register at broker startup; lookup
// is per-request and called once per matching CSR — small N (one plugin
// per supported transport, today just HTTP), so a linked list / array
// scan is fine.
//
#include <stdbool.h>                                   // bool
#include <stddef.h>                                    // NULL
#include <strings.h>                                   // strcasecmp
#include <string.h>                                    // strchr

#include "corNgsild/ldForwarding.h"                     // Own interface



// -----------------------------------------------------------------------------
//
// Module state
//
#define LD_FORWARDING_MAX_PLUGINS 8

static const LdForwardingPlugin* registry[LD_FORWARDING_MAX_PLUGINS];
static int                       registryCount = 0;



// -----------------------------------------------------------------------------
//
// schemeAlreadyClaimed -
//
static bool schemeAlreadyClaimed(const char* scheme)
{
  for (int i = 0; i < registryCount; i++)
  {
    const LdForwardingPlugin* p = registry[i];
    for (const char* const* s = p->schemes; s != NULL && *s != NULL; s++)
    {
      if (strcasecmp(*s, scheme) == 0)
        return true;
    }
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// ldForwardingRegister -
//
bool ldForwardingRegister(const LdForwardingPlugin* plugin)
{
  if (plugin == NULL || plugin->send == NULL || plugin->schemes == NULL)
    return false;

  // Refuse if any of the requested schemes is already taken
  for (const char* const* s = plugin->schemes; *s != NULL; s++)
  {
    if (schemeAlreadyClaimed(*s))
      return false;
  }

  if (registryCount >= LD_FORWARDING_MAX_PLUGINS)
    return false;

  registry[registryCount++] = plugin;
  return true;
}



// -----------------------------------------------------------------------------
//
// ldForwardingForScheme -
//
const LdForwardingPlugin* ldForwardingForScheme(const char* scheme)
{
  if (scheme == NULL)
    return NULL;

  for (int i = 0; i < registryCount; i++)
  {
    const LdForwardingPlugin* p = registry[i];
    for (const char* const* s = p->schemes; s != NULL && *s != NULL; s++)
    {
      if (strcasecmp(*s, scheme) == 0)
        return p;
    }
  }
  return NULL;
}



// -----------------------------------------------------------------------------
//
// ldForwardingForEndpoint -
//
const LdForwardingPlugin* ldForwardingForEndpoint(const char* endpoint)
{
  if (endpoint == NULL)
    return NULL;

  const char* colon = strchr(endpoint, ':');
  if (colon == NULL || colon == endpoint)
    return NULL;

  // Stack-copy the scheme so we can NUL-terminate without touching the input
  int schemeLen = colon - endpoint;
  if (schemeLen >= 16)
    return NULL;

  char schemeBuf[16];
  for (int i = 0; i < schemeLen; i++)
    schemeBuf[i] = endpoint[i];
  schemeBuf[schemeLen] = 0;

  return ldForwardingForScheme(schemeBuf);
}
