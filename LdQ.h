#ifndef SWNGSILD_LDQ_H_
#define SWNGSILD_LDQ_H_

//
// FILE            LdQ.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
// Types for the Q expression tree used by the ?q= URL parameter.
// Parsed by ldQParse(), translated to BSON by the DB plugin.
//
#include <stdbool.h>                                     // bool



// -----------------------------------------------------------------------------
//
// LdQOperator - comparison operator in a Q term
//
typedef enum LdQOperator
{
  LdQExists,      // no operator (attribute existence check)
  LdQEqual,       // ==
  LdQUnequal,     // !=
  LdQGreater,     // >
  LdQLess,        // <
  LdQGreaterEq,   // >=
  LdQLessEq,      // <=
  LdQPattern,     // ~=
  LdQNotPattern   // !~=
} LdQOperator;



// -----------------------------------------------------------------------------
//
// LdQValueType - type of the right-hand side value
//
typedef enum LdQValueType
{
  LdQNoValue,     // existence check (no RHS)
  LdQNumber,      // numeric literal
  LdQString,      // quoted string
  LdQBool,        // true/false
  LdQDateTime,    // ISO 8601 date-time
  LdQRange,       // lo..hi  (numeric)
  LdQDateRange,   // lo..hi  (date-time strings)
  LdQValueList    // v1,v2,...  (only with == or !=)
} LdQValueType;



// -----------------------------------------------------------------------------
//
// LdQValue - right-hand side value union
//
typedef struct LdQValue
{
  union
  {
    double  n;      // LdQNumber
    char*   s;      // LdQString, LdQDateTime, LdQPattern/LdQNotPattern
    bool    b;      // LdQBool

    struct { double lo; double hi; }                          numRange;   // LdQRange (numeric)
    struct { char*  lo; char*  hi; }                          dateRange;  // LdQRange (date)
    struct { char** values; int count; LdQValueType itemType; } list;     // LdQValueList
  };
} LdQValue;



// -----------------------------------------------------------------------------
//
// LdQTerm - a leaf node: attribute op value
//
typedef struct LdQTerm
{
  char*         attr;       // expanded attribute IRI
  LdQOperator   op;
  LdQValueType  valueType;
  LdQValue      value;
} LdQTerm;



// -----------------------------------------------------------------------------
//
// LdQNodeType - node types in the expression tree
//
typedef enum LdQNodeType
{
  LdQTermNode,    // leaf: single comparison or existence check
  LdQAndNode,     // AND group (';' separated)
  LdQOrNode       // OR group  ('|' separated)
} LdQNodeType;



// -----------------------------------------------------------------------------
//
// LdQNode - expression tree node (term or group)
//
typedef struct LdQNode
{
  LdQNodeType type;
  union
  {
    LdQTerm term;                                                        // LdQTermNode
    struct { struct LdQNode** childV; int count; int allocated; } group;  // LdQAndNode / LdQOrNode
  };
} LdQNode;

#endif  // SWNGSILD_LDQ_H_
