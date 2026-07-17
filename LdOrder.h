#ifndef SWNGSILD_LDORDER_H_
#define SWNGSILD_LDORDER_H_

#include <stdbool.h>                                   // bool

//
// FILE            LdOrder.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// NGSI-LD Entity Ordering (§ 4.23). Parsed from the ?orderBy= URL param.
//



// -----------------------------------------------------------------------------
//
// LdOrderDir - sort direction
//
typedef enum LdOrderDir
{
  LdOrderAsc = 0,    // default
  LdOrderDesc
} LdOrderDir;



// -----------------------------------------------------------------------------
//
// LdOrderTerm - one element of the orderBy expression
//
//   ?orderBy=speed;desc,name           → two terms (one path segment each)
//   ?orderBy=name.subProperty          → one term, two path segments
//
// `attrName`     joined dotted form (after ldExpandParams), kept for legacy
//                callers / logging — DO NOT walk it with strchr('.'), the
//                expanded IRIs contain dots themselves.
// `pathSegV`     NULL-terminated array of expanded segments (allocated from
//                the request arena). Use this for sort lookups.
// `pathSegN`     count of segments (== 1 for a flat orderBy term).
// `byDistance`   true for a "dist-asc"/"dist-desc" term (§ 7.6.2.2 sort by
//                distance): `attrName` names the GeoProperty and the ordering
//                key is each entity's computed `geoDistance` from `orderFrom`,
//                not an attribute value. GeoProperty-bearing entities rank ahead
//                of those without (which have no distance).
// `valuePathV`   § 7.6.2.3 trailing path — the dot-separated JSON member names
//                inside a single pair of square brackets (e.g. orderBy=
//                address[city] or address[a.b]). Descends INTO the compound
//                JSON value of the attribute (raw JSON keys, NOT @context-
//                expanded). NULL when the term has no trailing path.
// `valuePathN`   count of trailing-path members (0 when none).
//
typedef struct LdOrderTerm
{
  char*       attrName;
  char**      pathSegV;
  int         pathSegN;
  LdOrderDir  dir;
  bool        byDistance;
  char**      valuePathV;
  int         valuePathN;
} LdOrderTerm;

#endif  // SWNGSILD_LDORDER_H_
