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
// ?orderBy=speed;desc,name → two terms: {speed, desc}, {name, asc}
//
typedef struct LdOrderTerm
{
  char*       attrName;   // expanded IRI (after ldExpandParams)
  LdOrderDir  dir;        // asc or desc
} LdOrderTerm;

#endif  // SWNGSILD_LDORDER_H_
