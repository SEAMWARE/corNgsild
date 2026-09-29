//
// FILE            ldTenantHeader.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include "corRest/corRestClient.h"                       // CorRestClientRequest, corRestClientRequestHeader

#include "corNgsild/ldTenantHeader.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// ldTenantHeaderAdd -
//
void ldTenantHeaderAdd(CorRestClientRequest* reqP, const char* tenantName)
{
  if ((tenantName != NULL) && (tenantName[0] != 0))
    corRestClientRequestHeader(reqP, "NGSILD-Tenant", tenantName);
}
