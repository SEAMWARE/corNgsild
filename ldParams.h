#ifndef SWNGSILD_LDPARAMS_H_
#define SWNGSILD_LDPARAMS_H_

//
// FILE            ldParams.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stdint.h>                                      // uint64_t

#include "swRest/swRest.h"                             // SwRestParam



// -----------------------------------------------------------------------------
//
// NGSI-LD URL parameter bits
//
#define LD_PARAM_ID              (1ULL <<  0)
#define LD_PARAM_TYPE            (1ULL <<  1)
#define LD_PARAM_ID_PATTERN      (1ULL <<  2)
#define LD_PARAM_ATTRS           (1ULL <<  3)
#define LD_PARAM_Q               (1ULL <<  4)
#define LD_PARAM_GEOREL          (1ULL <<  5)
#define LD_PARAM_GEOMETRY         (1ULL <<  6)
#define LD_PARAM_COORDINATES     (1ULL <<  7)
#define LD_PARAM_GEOPROPERTY     (1ULL <<  8)
#define LD_PARAM_CSF             (1ULL <<  9)
#define LD_PARAM_LIMIT           (1ULL << 10)
#define LD_PARAM_OFFSET          (1ULL << 11)
#define LD_PARAM_COUNT           (1ULL << 12)
#define LD_PARAM_OPTIONS         (1ULL << 13)
#define LD_PARAM_PICK            (1ULL << 14)
#define LD_PARAM_OMIT            (1ULL << 15)
#define LD_PARAM_LANG            (1ULL << 16)
#define LD_PARAM_SCOPE_Q         (1ULL << 17)
#define LD_PARAM_FORMAT          (1ULL << 18)
#define LD_PARAM_ENTITY_MAP      (1ULL << 19)
#define LD_PARAM_LOCAL           (1ULL << 20)
#define LD_PARAM_VIA             (1ULL << 21)
#define LD_PARAM_DELETE_ALL      (1ULL << 22)
#define LD_PARAM_TIMEPROPERTY    (1ULL << 23)
#define LD_PARAM_TIMEREL         (1ULL << 24)
#define LD_PARAM_TIMEAT          (1ULL << 25)
#define LD_PARAM_ENDTIMEAT       (1ULL << 26)
#define LD_PARAM_LAST_N          (1ULL << 27)
#define LD_PARAM_AGGR_METHODS    (1ULL << 28)
#define LD_PARAM_AGGR_PERIOD_DURATION  (1ULL << 29)
#define LD_PARAM_SYSATTRS              (1ULL << 30)
#define LD_PARAM_DATASETID             (1ULL << 31)
#define LD_PARAM_EXPAND_VALUES         (1ULL << 32)
#define LD_PARAM_JSON_KEYS             (1ULL << 33)
#define LD_PARAM_GEOMETRY_PROPERTY     (1ULL << 34)
#define LD_PARAM_OBSERVED_AT           (1ULL << 35)
#define LD_PARAM_DETAILS               (1ULL << 36)
#define LD_PARAM_KIND                  (1ULL << 37)
#define LD_PARAM_RELOAD                (1ULL << 38)
#define LD_PARAM_ORDER_BY              (1ULL << 39)
#define LD_PARAM_COLLATION             (1ULL << 40)
#define LD_PARAM_SPLIT_ENTITIES        (1ULL << 41)



// -----------------------------------------------------------------------------
//
// Prebuilt supported-params bitmasks per service
//
#define LD_PARAMS_GET_ENTITIES    \
  ( LD_PARAM_ID          \
  | LD_PARAM_TYPE        \
  | LD_PARAM_ID_PATTERN  \
  | LD_PARAM_ATTRS       \
  | LD_PARAM_Q           \
  | LD_PARAM_GEOREL      \
  | LD_PARAM_GEOMETRY    \
  | LD_PARAM_COORDINATES \
  | LD_PARAM_GEOPROPERTY \
  | LD_PARAM_CSF         \
  | LD_PARAM_LIMIT       \
  | LD_PARAM_OFFSET      \
  | LD_PARAM_COUNT       \
  | LD_PARAM_OPTIONS     \
  | LD_PARAM_PICK        \
  | LD_PARAM_OMIT        \
  | LD_PARAM_LANG        \
  | LD_PARAM_SCOPE_Q     \
  | LD_PARAM_FORMAT      \
  | LD_PARAM_ENTITY_MAP  \
  | LD_PARAM_LOCAL       \
  | LD_PARAM_VIA         \
  | LD_PARAM_SYSATTRS       \
  | LD_PARAM_DATASETID      \
  | LD_PARAM_EXPAND_VALUES  \
  | LD_PARAM_JSON_KEYS           \
  | LD_PARAM_GEOMETRY_PROPERTY   \
  | LD_PARAM_ORDER_BY            \
  | LD_PARAM_COLLATION           \
  | LD_PARAM_SPLIT_ENTITIES      \
  )

#define LD_PARAMS_GET_ENTITY     \
  ( LD_PARAM_TYPE           \
  | LD_PARAM_ATTRS          \
  | LD_PARAM_OPTIONS        \
  | LD_PARAM_PICK           \
  | LD_PARAM_OMIT           \
  | LD_PARAM_LANG           \
  | LD_PARAM_FORMAT         \
  | LD_PARAM_SYSATTRS       \
  | LD_PARAM_DATASETID      \
  | LD_PARAM_EXPAND_VALUES  \
  | LD_PARAM_JSON_KEYS           \
  | LD_PARAM_GEOMETRY_PROPERTY   \
  | LD_PARAM_LOCAL               \
  )

#define LD_PARAMS_POST_ENTITIES  ( LD_PARAM_LOCAL )

#define LD_PARAMS_POST_ENTITY_ATTRS  \
  ( LD_PARAM_TYPE    \
  | LD_PARAM_OPTIONS \
  | LD_PARAM_LOCAL   \
  )

#define LD_PARAMS_REPLACE_ENTITY  ( LD_PARAM_TYPE | LD_PARAM_LOCAL )

#define LD_PARAMS_DELETE_ENTITY   ( LD_PARAM_TYPE | LD_PARAM_LOCAL )

#define LD_PARAMS_PATCH_ENTITY  \
  ( LD_PARAM_TYPE         \
  | LD_PARAM_FORMAT       \
  | LD_PARAM_LANG         \
  | LD_PARAM_OBSERVED_AT  \
  | LD_PARAM_OPTIONS      \
  | LD_PARAM_LOCAL        \
  )

#define LD_PARAMS_POST_SUBSCRIPTIONS   0

#define LD_PARAMS_GET_SUBSCRIPTIONS    \
  ( LD_PARAM_LIMIT       \
  | LD_PARAM_OFFSET      \
  | LD_PARAM_COUNT       \
  )

#define LD_PARAMS_GET_SUBSCRIPTION     0

#define LD_PARAMS_PATCH_SUBSCRIPTION   0

#define LD_PARAMS_DELETE_SUBSCRIPTION  0

#define LD_PARAMS_GET_JSONLD_CONTEXTS  \
  ( LD_PARAM_DETAILS  \
  | LD_PARAM_KIND     \
  | LD_PARAM_LIMIT    \
  | LD_PARAM_OFFSET   \
  | LD_PARAM_COUNT    \
  )

#define LD_PARAMS_GET_JSONLD_CONTEXT   0
#define LD_PARAMS_POST_JSONLD_CONTEXTS 0
#define LD_PARAMS_DELETE_JSONLD_CONTEXT  ( LD_PARAM_RELOAD )

#define LD_PARAMS_POST_CSOURCE_REGISTRATIONS  0

#define LD_PARAMS_GET_CSOURCE_REGISTRATIONS   \
  ( LD_PARAM_TYPE         \
  | LD_PARAM_ID           \
  | LD_PARAM_ID_PATTERN   \
  | LD_PARAM_ATTRS        \
  | LD_PARAM_Q            \
  | LD_PARAM_GEOMETRY     \
  | LD_PARAM_COORDINATES  \
  | LD_PARAM_GEOREL       \
  | LD_PARAM_GEOPROPERTY  \
  | LD_PARAM_CSF          \
  | LD_PARAM_SCOPE_Q      \
  | LD_PARAM_LIMIT        \
  | LD_PARAM_OFFSET       \
  | LD_PARAM_COUNT        \
  )

#define LD_PARAMS_GET_CSOURCE_REGISTRATION    0
#define LD_PARAMS_PATCH_CSOURCE_REGISTRATION  0
#define LD_PARAMS_DELETE_CSOURCE_REGISTRATION 0



// -----------------------------------------------------------------------------
//
// ldParamRegistryV - parameter name-to-bit registry (NULL-terminated)
//
extern SwRestParam ldParamRegistryV[];



// -----------------------------------------------------------------------------
//
// ldParamsInit - register all NGSI-LD URL parameters with swRest
//
extern bool ldParamsInit(void);

#endif  // SWNGSILD_LDPARAMS_H_
