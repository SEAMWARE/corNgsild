#ifndef CORNGSILD_H_
#define CORNGSILD_H_

//
// FILE            corNgsild.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
// 
//
#include "corNgsild/LdAttrType.h"                         // LdAttrType
#include "corNgsild/LdFormat.h"                           // LdFormat
#include "corNgsild/LdOp.h"                               // LdOp
#include "corNgsild/LdProblem.h"                          // LD_ERROR_*
#include "corNgsild/LdCheck.h"                            // OBJECT_CHECK, STRING_CHECK, ...
#include "corNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "corNgsild/ldTraceLevels.h"                      // LdTInit, etc.
#include "corNgsild/ldError.h"                            // ldError
#include "corNgsild/ldInit.h"                             // ldInit, ldCleanup
#include "corNgsild/ldParams.h"                           // ldParamsInit, LD_PARAM_*, ldParams*
#include "corNgsild/ldTypes.h"                            // ldAttrTypeToString, etc.
#include "corNgsild/ldAttrTypeDetect.h"                   // ldAttrTypeDetect
#include "corNgsild/ldCheckUri.h"                         // ldCheckUri
#include "corNgsild/ldCheckDateTime.h"                    // ldCheckDateTime
#include "corNgsild/ldCheckGeo.h"                         // ldCheckGeo
#include "corNgsild/ldCheckEntity.h"                      // ldCheckEntity
#include "corNgsild/ldCheckAttribute.h"                   // ldCheckAttribute
#include "corNgsild/ldCheckSubscription.h"                // ldCheckSubscription
#include "corNgsild/ldCheckRegistration.h"                // ldCheckRegistration
#include "corNgsild/ldRender.h"                            // ldToNormalized, ldToConcise, ldToSimplified
#include "corNgsild/ldQueryParams.h"                       // ldQueryParamValue, ldParamSplit, ldParamExpandV
#include "corNgsild/ldPickOmit.h"                          // ldPickOmit
#include "corNgsild/ldStripSysAttrs.h"                     // ldStripSysAttrs
#include "corNgsild/ldApiEntityToDbModel.h"                 // ldApiEntityToDbModel
#include "corNgsild/ldEntityToApi.h"                       // ldEntityToApi
#include "corNgsild/LdScopeExpr.h"                          // LdScopeExpr, ldScopeExprParse
#include "corNgsild/LdTypeExpr.h"                           // LdTypeExpr, ldTypeExprParse
#include "corNgsild/CorNgsild.h"                            // CorNgsild, corNgsild, ldParamHook
#include "corNgsild/ldHooks.h"                             // ldHooksRegister
#include "corNgsild/ldPagination.h"                        // ldPaginationTrim, ldPaginationLinkHeader



// -----------------------------------------------------------------------------
//
// CORNGSILD_VERSION
//
#define CORNGSILD_VERSION "post-0.2.0"



// -----------------------------------------------------------------------------
//
// corNgsildVersion -
//
extern const char* corNgsildVersion;

#endif  // CORNGSILD_H_
