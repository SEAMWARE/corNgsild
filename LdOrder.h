#ifndef SWNGSILD_LDORDER_H_
#define SWNGSILD_LDORDER_H_

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
//
typedef struct LdOrderTerm
{
  char*       attrName;
  char**      pathSegV;
  int         pathSegN;
  LdOrderDir  dir;
} LdOrderTerm;

#endif  // SWNGSILD_LDORDER_H_
