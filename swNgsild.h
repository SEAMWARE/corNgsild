#ifndef SWNGSILD_SWNGSILD_H_
#define SWNGSILD_SWNGSILD_H_

//
// FILE            swNgsild.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include "swNgsild/LdAttrType.h"                         // LdAttrType
#include "swNgsild/LdFormat.h"                           // LdFormat
#include "swNgsild/LdOp.h"                               // LdOp
#include "swNgsild/LdProblem.h"                          // LD_ERROR_*
#include "swNgsild/LdCheck.h"                            // OBJECT_CHECK, STRING_CHECK, ...
#include "swNgsild/LdVocab.h"                            // LD_VOCAB_*
#include "swNgsild/ldTraceLevels.h"                      // LdTInit, etc.
#include "swNgsild/ldError.h"                            // ldError
#include "swNgsild/ldInit.h"                             // ldInit, ldCleanup
#include "swNgsild/ldParams.h"                           // ldParamsInit, LD_PARAM_*, ldParams*
#include "swNgsild/ldTypes.h"                            // ldAttrTypeToString, etc.
#include "swNgsild/ldAttrTypeDetect.h"                   // ldAttrTypeDetect
#include "swNgsild/ldCheckUri.h"                         // ldCheckUri
#include "swNgsild/ldCheckDateTime.h"                    // ldCheckDateTime
#include "swNgsild/ldCheckGeo.h"                         // ldCheckGeo
#include "swNgsild/ldCheckEntity.h"                      // ldCheckEntity
#include "swNgsild/ldCheckAttribute.h"                   // ldCheckAttribute
#include "swNgsild/ldCheckSubscription.h"                // ldCheckSubscription
#include "swNgsild/ldCheckRegistration.h"                // ldCheckRegistration
#include "swNgsild/ldRender.h"                            // ldToNormalized, ldToConcise, ldToSimplified
#include "swNgsild/ldQueryParams.h"                       // ldQueryParamValue, ldParamSplit, ldParamExpandV
#include "swNgsild/ldPickOmit.h"                          // ldPickOmit
#include "swNgsild/ldStripSysAttrs.h"                     // ldStripSysAttrs
#include "swNgsild/ldApiEntityToDbModel.h"                 // ldApiEntityToDbModel
#include "swNgsild/ldEntityToApi.h"                       // ldEntityToApi
#include "swNgsild/LdScopeExpr.h"                          // LdScopeExpr, ldScopeExprParse
#include "swNgsild/LdTypeExpr.h"                           // LdTypeExpr, ldTypeExprParse
#include "swNgsild/SwNgsild.h"                            // SwNgsild, swNgsild, ldParamHook
#include "swNgsild/ldHooks.h"                             // ldHooksRegister
#include "swNgsild/ldPagination.h"                        // ldPaginationTrim, ldPaginationLinkHeader



// -----------------------------------------------------------------------------
//
// SWNGSILD_VERSION
//
#define SWNGSILD_VERSION "post-0.2.0"



// -----------------------------------------------------------------------------
//
// swNgsildVersion -
//
extern const char* swNgsildVersion;

#endif  // SWNGSILD_SWNGSILD_H_
