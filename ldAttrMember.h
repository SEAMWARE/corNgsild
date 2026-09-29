#ifndef CORNGSILD_LD_ATTR_MEMBER_H_
#define CORNGSILD_LD_ATTR_MEMBER_H_

//
// FILE            ldAttrMember.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdint.h>                                      // uint32_t

#include "corTree/CorNode.h"                            // CorNode
#include "corNgsild/CorTerm.h"                          // CorTermLast
#include "corNgsild/LdAttrType.h"                       // LdAttrType
#include "corNgsild/ldTermId.h"                         // ldTermId



// -----------------------------------------------------------------------------
//
// The MEMBERS of an Attribute - the core terms an Attribute (of any type) carries as
// its own, non-reified members (TS 104 175 clause 5.2.5 matrix, plus "lang" of a
// lang=-reduced LanguageProperty). Every other member of an Attribute is a
// sub-attribute. One bit each, so "what is this Attribute made of" is an OR per member
// and "is any of it wrong" one test per Attribute (KZ's design, 2026-09-28).
//
// ONE list, replacing four that had drifted apart (ldRender, ldLangReduce,
// ldCheckAttribute, the expander's KJF_ATTR_TERM).
//
#define LD_AM_TYPE                   (1u <<  0)
#define LD_AM_VALUE                  (1u <<  1)
#define LD_AM_OBJECT                 (1u <<  2)
#define LD_AM_OBJECT_LIST            (1u <<  3)
#define LD_AM_LANGUAGE_MAP           (1u <<  4)
#define LD_AM_VOCAB                  (1u <<  5)
#define LD_AM_VALUE_LIST             (1u <<  6)
#define LD_AM_JSON                   (1u <<  7)
#define LD_AM_OBSERVED_AT            (1u <<  8)
#define LD_AM_UNIT_CODE              (1u <<  9)
#define LD_AM_DATASET_ID             (1u << 10)
#define LD_AM_VALUE_TYPE             (1u << 11)
#define LD_AM_OBJECT_TYPE            (1u << 12)
#define LD_AM_EXPIRES_AT             (1u << 13)
#define LD_AM_CREATED_AT             (1u << 14)
#define LD_AM_MODIFIED_AT            (1u << 15)
#define LD_AM_DELETED_AT             (1u << 16)
#define LD_AM_INSTANCE_ID            (1u << 17)
#define LD_AM_PREVIOUS_VALUE         (1u << 18)
#define LD_AM_PREVIOUS_OBJECT        (1u << 19)
#define LD_AM_PREVIOUS_OBJECT_LIST   (1u << 20)
#define LD_AM_PREVIOUS_LANGUAGE_MAP  (1u << 21)
#define LD_AM_PREVIOUS_VOCAB         (1u << 22)
#define LD_AM_PREVIOUS_VALUE_LIST    (1u << 23)
#define LD_AM_PREVIOUS_JSON          (1u << 24)
#define LD_AM_NGSILDPROOF            (1u << 25)
#define LD_AM_ENTITY                 (1u << 26)
#define LD_AM_ENTITY_LIST            (1u << 27)
#define LD_AM_LANG                   (1u << 28)



// -----------------------------------------------------------------------------
//
// LdMemberSource - where an Attribute came from, which decides what its members may be
//
//   entity request    a client's Entity payload: broker-generated members are a 400
//                     (instanceId "only used in temporal representation", previous*
//                     "only used in Notifications", and core non-reified terms "shall not
//                     be used as Attribute names"); createdAt/modifiedAt are ignored
//   temporal request  a client's temporal payload: instanceId and deletedAt are ignored
//                     too - "systems should maintain an instanceId", a client does not
//   context source    a context source's response: everything kept - the timestamps are
//                     what tells several copies of one Attribute apart (4.5.5.3 rule 3)
//   db                what the broker stored itself: everything kept
//
typedef enum LdMemberSource
{
  LdSourceEntityRequest = 0,
  LdSourceTemporalRequest,
  LdSourceContextSource,
  LdSourceDb,
  LdSourceCount
} LdMemberSource;



// -----------------------------------------------------------------------------
//
// ldAttrMemberBit - the LD_AM_* bit of each CorTerm; 0: not an Attribute member
//
extern const uint32_t ldAttrMemberBit[CorTermLast];



// -----------------------------------------------------------------------------
//
// ldAttrKeep / ldAttrDrop - per source and Attribute type: the members kept, and the
// members silently removed. A member in neither is forbidden (400).
//
extern const uint32_t ldAttrKeep[LdSourceCount][LdAttrJsonProperty + 1];
extern const uint32_t ldAttrDrop[LdSourceCount][LdAttrJsonProperty + 1];



// -----------------------------------------------------------------------------
//
// ldAttrMemberOf - the member bit of a node (0: a sub-attribute, or not in an Attribute)
//
static inline uint32_t ldAttrMemberOf(CorNode* nodeP)
{
  return ldAttrMemberBit[ldTermId(nodeP)];
}



// -----------------------------------------------------------------------------
//
// ldAttrMembersStrip - remove the members ldAttrDrop names for this source and type
//
// For a path that does not run ldCheckAttribute (the temporal routes).
//
extern void ldAttrMembersStrip(CorNode* attrP, LdMemberSource source, LdAttrType attrType);



// -----------------------------------------------------------------------------
//
// ldTemporalMembersStrip - a client's temporal payload: drop, from every instance of
// every Attribute, what the broker maintains itself (instanceId, deletedAt, createdAt,
// modifiedAt - LdSourceTemporalRequest). An Attribute is one instance or an array of them.
//
extern void ldTemporalMembersStrip(CorNode* entityP);

#endif  // CORNGSILD_LD_ATTR_MEMBER_H_
