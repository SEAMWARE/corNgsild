//
// FILE            ldCsourceAlias.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stddef.h>                                  // NULL
#include <string.h>                                  // strlen, strcpy, strncmp
#include <strings.h>                                 // strcasecmp

#include "kalloc/KAlloc.h"                           // KAlloc
#include "kalloc/kaAlloc.h"                          // kaAlloc
#include "corRest/CorRestKeyValue.h"                   // CorRestKeyValue
#include "corNgsild/CorNgsild.h"                       // ldCsourceAliasBase

#include "corNgsild/ldCsourceAlias.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// ldCsourceAliasForTenant -
//
const char* ldCsourceAliasForTenant(const char* tenant, KAlloc* kaP)
{
  if (ldCsourceAliasBase == NULL)
    return NULL;

  if (tenant == NULL || tenant[0] == 0)
    return ldCsourceAliasBase;

  int   baseLen   = strlen(ldCsourceAliasBase);
  int   tenantLen = strlen(tenant);
  char* aliasP    = (char*) kaAlloc(kaP, baseLen + 1 + tenantLen + 1);

  strcpy(aliasP, ldCsourceAliasBase);
  aliasP[baseLen] = ':';
  strcpy(aliasP + baseLen + 1, tenant);

  return aliasP;
}



// -----------------------------------------------------------------------------
//
// viaEntryMatchesAlias - parse one Via entry and compare its pseudonym to alias
//
// One Via entry per RFC 7230 § 5.7.1:
//   received-protocol RWS received-by [ RWS comment ]
// e.g. "1.1 cb-alias", "HTTP/1.1 host:port (comment)".
// We skip the protocol token, then capture the received-by token up to the
// next whitespace, ',' or '(', and string-equal it against alias.
//
static bool viaEntryMatchesAlias(const char* entry, int entryLen, const char* alias, int aliasLen)
{
  int p = 0;

  // skip leading whitespace
  while (p < entryLen && (entry[p] == ' ' || entry[p] == '\t')) p++;

  // skip protocol token (1*VCHAR up to whitespace)
  while (p < entryLen && entry[p] != ' ' && entry[p] != '\t') p++;

  // skip whitespace between protocol and received-by
  while (p < entryLen && (entry[p] == ' ' || entry[p] == '\t')) p++;

  // received-by token: up to whitespace, ',' or '('
  int start = p;
  while (p < entryLen && entry[p] != ' ' && entry[p] != '\t' && entry[p] != ',' && entry[p] != '(') p++;

  int tokenLen = p - start;
  if (tokenLen != aliasLen)
    return false;

  return strncmp(entry + start, alias, aliasLen) == 0;
}



// -----------------------------------------------------------------------------
//
// ldViaHasAlias -
//
bool ldViaHasAlias(CorRestKeyValue* httpHeaderV, int httpHeaderCount, const char* alias)
{
  if (alias == NULL || httpHeaderV == NULL || httpHeaderCount == 0)
    return false;

  int aliasLen = strlen(alias);

  for (int i = 0; i < httpHeaderCount; i++)
  {
    if (httpHeaderV[i].key == NULL || httpHeaderV[i].value == NULL)
      continue;
    if (strcasecmp(httpHeaderV[i].key, "Via") != 0)
      continue;

    // Split header value on ',' — each fragment is one Via entry
    const char* v   = httpHeaderV[i].value;
    int         vlen = strlen(v);
    int         entryStart = 0;

    for (int p = 0; p <= vlen; p++)
    {
      if (p == vlen || v[p] == ',')
      {
        if (viaEntryMatchesAlias(v + entryStart, p - entryStart, alias, aliasLen))
          return true;
        entryStart = p + 1;
      }
    }
  }
  return false;
}
