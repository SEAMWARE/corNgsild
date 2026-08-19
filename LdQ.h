#ifndef CORNGSILD_LDQ_H_
#define CORNGSILD_LDQ_H_

//
// FILE            LdQ.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
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
  LdQNotExists,   // !attr (attribute non-existence check)
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
    double     n;      // LdQNumber
    char*      s;      // LdQString, LdQPattern/LdQNotPattern
    bool       b;      // LdQBool
    long long  ns;     // LdQDateTime (nanoseconds since epoch)

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
  char*         attr;       // expanded attribute IRI (first path segment)
  char**        subPathV;   // § 4.9 attrPath — expanded sub-attribute IRIs
                            // (segments after the first '.'), or NULL.
                            // Path dots are separators on the wire; IRI
                            // dots travel %2E-encoded (ldQRender) and are
                            // decoded segment-wise at parse.
  int           subPathN;
  char**        valuePathV;  // § 4.9 "[...]" — path INTO the value: opaque
                             // JSON member names (NO context expansion /
                             // compaction), dot-separated inside the
                             // brackets, %XX round-trip-safe.
  int           valuePathN;
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
  LdQOrNode,      // OR group  ('|' separated)
  LdQLinkedNode   // attrName "{" sub-q "}" — § 4.9 LinkedEntityRelation
} LdQNodeType;



// -----------------------------------------------------------------------------
//
// LdQLinked - attrName "{" sub-q "}" linked-entity sub-query
//
// Evaluated against the *target* of the named Relationship attribute on
// the entity under test. The sub-query is parsed recursively into its
// own LdQNode tree.
//
typedef struct LdQLinked
{
  char*           relName;   // expanded IRI of the Relationship attribute
  struct LdQNode* subQ;      // sub-expression evaluated against the target entity
} LdQLinked;



// -----------------------------------------------------------------------------
//
// LdQNode - expression tree node (term or group)
//
typedef struct LdQNode
{
  LdQNodeType type;
  union
  {
    LdQTerm   term;                                                       // LdQTermNode
    LdQLinked linked;                                                     // LdQLinkedNode
    struct { struct LdQNode** childV; int count; int allocated; } group;  // LdQAndNode / LdQOrNode
  };
  // Deepest chain of nested LdQLinkedNodes at/under this node — set by ldQParse
  // as a side effect. The ROOT's value is the q's relationship-follow depth,
  // bounded by joinLevel (see the q-vs-joinLevel 400 check in getEntities).
  int linkedDepth;
} LdQNode;

#endif  // CORNGSILD_LDQ_H_
