#ifndef CORNGSILD_LDTENANTHEADER_H_
#define CORNGSILD_LDTENANTHEADER_H_

//
// FILE            ldTenantHeader.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include "corRest/corRestClient.h"                       // CorRestClientRequest



// -----------------------------------------------------------------------------
//
// ldTenantHeaderAdd - NGSILD-Tenant on an outgoing notification, unless the tenant is the default
//
// § 6.4.8: a notification of a subscription on a non-default tenant carries the tenant - without
// it the entity ids in the body are ambiguous. The tenant is the SUBSCRIPTION's: a request-driven
// notification finds it in corNgsild.tenantName (the change happened in the subscription's
// tenant), a periodic tick sets corNgsild.tenantName for the tenant it visits, a periodic
// subscription carries its own. NULL or "" = the default tenant: no header.
//
extern void ldTenantHeaderAdd(CorRestClientRequest* reqP, const char* tenantName);

#endif  // CORNGSILD_LDTENANTHEADER_H_
