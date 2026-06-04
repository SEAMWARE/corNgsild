//
// FILE            ldParams.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#include <stddef.h>                                      // NULL
#include <stdbool.h>                                     // bool

#include "swRest/swRest.h"                             // SwRestParam, swRestParamInit

#include "swNgsild/ldParams.h"                           // Own interface



// -----------------------------------------------------------------------------
//
// ldParamRegistryV - parameter name-to-bit registry
//
SwRestParam ldParamRegistryV[] =
{
  { "id",                    LD_PARAM_ID },
  { "type",                  LD_PARAM_TYPE },
  { "idPattern",             LD_PARAM_ID_PATTERN },
  { "attrs",                 LD_PARAM_ATTRS },
  { "q",                     LD_PARAM_Q },
  { "georel",                LD_PARAM_GEOREL },
  { "geometry",              LD_PARAM_GEOMETRY },
  { "coordinates",           LD_PARAM_COORDINATES },
  { "geoproperty",           LD_PARAM_GEOPROPERTY },
  { "csf",                   LD_PARAM_CSF },
  { "limit",                 LD_PARAM_LIMIT },
  { "offset",                LD_PARAM_OFFSET },
  { "page",                  LD_PARAM_PAGE },
  { "count",                 LD_PARAM_COUNT },
  { "options",               LD_PARAM_OPTIONS },
  { "pick",                  LD_PARAM_PICK },
  { "omit",                  LD_PARAM_OMIT },
  { "lang",                  LD_PARAM_LANG },
  { "scopeQ",                LD_PARAM_SCOPE_Q },
  { "format",                LD_PARAM_FORMAT },
  { "entityMap",             LD_PARAM_ENTITY_MAP },
  { "local",                 LD_PARAM_LOCAL },
  { "noForward",             LD_PARAM_NO_FORWARD },
  { "hops",                  LD_PARAM_HOPS },
  { "drop",                  LD_PARAM_DROP },
  { "keep",                  LD_PARAM_KEEP },
  { "via",                   LD_PARAM_VIA },
  { "deleteAll",             LD_PARAM_DELETE_ALL },
  { "timeproperty",          LD_PARAM_TIMEPROPERTY },
  { "timerel",               LD_PARAM_TIMEREL },
  { "timeAt",                LD_PARAM_TIMEAT },
  { "endTimeAt",             LD_PARAM_ENDTIMEAT },
  { "lastN",                 LD_PARAM_LAST_N },
  { "firstN",                LD_PARAM_FIRST_N },
  { "offsetN",               LD_PARAM_OFFSET_N },
  { "aggrMethods",           LD_PARAM_AGGR_METHODS },
  { "aggrPeriodDuration",    LD_PARAM_AGGR_PERIOD_DURATION },
  { "sysAttrs",              LD_PARAM_SYSATTRS },
  { "datasetId",             LD_PARAM_DATASETID },
  { "expandValues",          LD_PARAM_EXPAND_VALUES },
  { "jsonKeys",              LD_PARAM_JSON_KEYS },
  { "geometryProperty",      LD_PARAM_GEOMETRY_PROPERTY },
  { "observedAt",            LD_PARAM_OBSERVED_AT },
  { "details",               LD_PARAM_DETAILS },
  { "kind",                  LD_PARAM_KIND },
  { "reload",                LD_PARAM_RELOAD },
  { "orderBy",               LD_PARAM_ORDER_BY },
  { "collation",             LD_PARAM_COLLATION },
  { "splitEntities",         LD_PARAM_SPLIT_ENTITIES },
  { "join",                  LD_PARAM_JOIN },
  { "joinLevel",             LD_PARAM_JOIN_LEVEL },
  { "containedBy",           LD_PARAM_CONTAINED_BY },
  { NULL,                    0 }
};





// -----------------------------------------------------------------------------
//
// ldParamsInit - register all NGSI-LD URL parameters with swRest
//
bool ldParamsInit(void)
{
  return swRestParamInit(ldParamRegistryV);
}
