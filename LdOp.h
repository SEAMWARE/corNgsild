//
// FILE            LdOp.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// NGSI-LD atomic operation identifiers.
//
// Values are powers of two so an LdOp can be used as both a single-op
// identifier (== / switch) and a bit in a mask of supported ops (OR /
// AND). The cache of Context Source Registrations stores the union of
// ops a CSR supports as an OR of LdOp bits, and the distributed
// dispatcher checks support with a single AND — no strcmp walks.
//
#ifndef LD_OP_H
#define LD_OP_H

#include <stdint.h>                                    // uint64_t



// -----------------------------------------------------------------------------
//
// LdOp - atomic NGSI-LD operations, one-bit-each
//
typedef enum LdOp
{
  LdOpNone                           = 0,

  // Entity write ops (§ 4.20)
  LdOpCreateEntity                   = 1ULL <<  0,
  LdOpUpdateEntity                   = 1ULL <<  1,
  LdOpAppendAttrs                    = 1ULL <<  2,
  LdOpUpdateAttrs                    = 1ULL <<  3,
  LdOpMergeEntity                    = 1ULL <<  4,
  LdOpReplaceEntity                  = 1ULL <<  5,
  LdOpDeleteEntity                   = 1ULL <<  6,
  LdOpDeleteAttr                     = 1ULL <<  7,
  LdOpReplaceAttr                    = 1ULL <<  8,
  LdOpPurgeEntity                    = 1ULL <<  9,

  // Entity read ops
  LdOpRetrieveEntity                 = 1ULL << 10,
  LdOpQueryEntities                  = 1ULL << 11,

  // Batch ops
  LdOpBatchCreate                    = 1ULL << 12,
  LdOpBatchUpsert                    = 1ULL << 13,
  LdOpBatchUpdate                    = 1ULL << 14,
  LdOpBatchDelete                    = 1ULL << 15,
  LdOpBatchMerge                     = 1ULL << 16,
  LdOpBatchQuery                     = 1ULL << 17,

  // Types / attribute-types metadata
  LdOpRetrieveEntityTypes            = 1ULL << 18,
  LdOpRetrieveEntityTypeDetails      = 1ULL << 19,
  LdOpRetrieveEntityTypeInfo         = 1ULL << 20,
  LdOpRetrieveAttrTypes              = 1ULL << 21,
  LdOpRetrieveAttrTypeDetails        = 1ULL << 22,
  LdOpRetrieveAttrTypeInfo           = 1ULL << 23,

  // Subscription / registration ops — not forwardable through a CSR's
  // operations[] field, but kept here so every service routine has a
  // distinct LdOp identity for logging / tracing.
  LdOpCreateSubscription             = 1ULL << 24,
  LdOpUpdateSubscription             = 1ULL << 25,
  LdOpRetrieveSubscription           = 1ULL << 26,
  LdOpQuerySubscription              = 1ULL << 27,
  LdOpDeleteSubscription             = 1ULL << 28,
  LdOpCreateRegistration             = 1ULL << 29,
  LdOpUpdateRegistration             = 1ULL << 30,
  LdOpRetrieveRegistration           = 1ULL << 31,
  LdOpQueryRegistration              = 1ULL << 32,
  LdOpDeleteRegistration             = 1ULL << 33,

  // Context Source Registration Subscription ops (§ 5.11)
  LdOpCreateCsourceSubscription      = 1ULL << 34,
  LdOpUpdateCsourceSubscription      = 1ULL << 35,
  LdOpRetrieveCsourceSubscription    = 1ULL << 36,
  LdOpQueryCsourceSubscription       = 1ULL << 37,
  LdOpDeleteCsourceSubscription      = 1ULL << 38,

  // Temporal API (§ 5.6.11-§ 5.6.16, § 5.7.3, § 5.7.4). Per § 4.20
  // Table 4.20-2 these are NOT in any default group — a CSR must
  // explicitly list them in operations[] to receive temporal forwards.
  LdOpUpsertTemporal                 = 1ULL << 39,   // § 5.6.11 — POST /temporal/entities
  LdOpAppendAttrsTemporal            = 1ULL << 40,   // § 5.6.12 — POST /temporal/entities/{id}/attrs
  LdOpDeleteAttrsTemporal            = 1ULL << 41,   // § 5.6.13 — DELETE /temporal/entities/{id}/attrs/{attr}
  LdOpUpdateAttrInstanceTemporal     = 1ULL << 42,   // § 5.6.14 — PATCH instance
  LdOpDeleteAttrInstanceTemporal     = 1ULL << 43,   // § 5.6.15 — DELETE instance
  LdOpDeleteTemporal                 = 1ULL << 44,   // § 5.6.16 — DELETE /temporal/entities/{id}
  LdOpRetrieveTemporal               = 1ULL << 45,   // § 5.7.3 — GET /temporal/entities/{id}
  LdOpQueryTemporal                  = 1ULL << 46,   // § 5.7.4 — GET /temporal/entities
  LdOpCreateEntityMapQueryTemporal   = 1ULL << 47,   // § 5.14.5 — entity-map for temporal query

  // Snapshot ops (§ 5.16). Implicitly local; not forwardable.
  LdOpCreateSnapshot                 = 1ULL << 48,   // § 5.16.1
  LdOpCloneSnapshot                  = 1ULL << 49,   // § 5.16.2
  LdOpRetrieveSnapshot               = 1ULL << 50,   // § 5.16.3
  LdOpUpdateSnapshot                 = 1ULL << 51,   // § 5.16.4
  LdOpDeleteSnapshot                 = 1ULL << 52,   // § 5.16.5
  LdOpPurgeSnapshots                 = 1ULL << 53    // § 5.16.7
} LdOp;



// -----------------------------------------------------------------------------
//
// Operation groups (§ 4.20 Table 4.20-2).
//
// A CSR's operations[] may contain atomic op names OR these group names.
// At parse time each group name is resolved to the OR of its member
// bits.
//
#define LD_OP_GROUP_RETRIEVE     \
  (LdOpRetrieveEntity | LdOpQueryEntities)

#define LD_OP_GROUP_UPDATE       \
  (LdOpUpdateEntity | LdOpUpdateAttrs | LdOpReplaceEntity | LdOpReplaceAttr)

#define LD_OP_GROUP_FEDERATION   \
  (LdOpRetrieveEntity | LdOpQueryEntities | LdOpBatchQuery | \
   LdOpRetrieveEntityTypes | LdOpRetrieveEntityTypeDetails | LdOpRetrieveEntityTypeInfo | \
   LdOpRetrieveAttrTypes   | LdOpRetrieveAttrTypeDetails   | LdOpRetrieveAttrTypeInfo)

#define LD_OP_GROUP_ASSOCIATION  LD_OP_GROUP_FEDERATION

#define LD_OP_GROUP_REDIRECTION  \
  (LdOpCreateEntity | LdOpUpdateEntity  | LdOpAppendAttrs | LdOpUpdateAttrs | \
   LdOpDeleteAttr   | LdOpDeleteEntity  | LdOpMergeEntity | \
   LdOpReplaceEntity | LdOpReplaceAttr  | LdOpRetrieveEntity | LdOpQueryEntities | \
   LdOpPurgeEntity | \
   LdOpRetrieveEntityTypes | LdOpRetrieveEntityTypeDetails | LdOpRetrieveEntityTypeInfo | \
   LdOpRetrieveAttrTypes   | LdOpRetrieveAttrTypeDetails   | LdOpRetrieveAttrTypeInfo)

#define LD_OP_GROUP_BATCH        \
  (LdOpBatchCreate | LdOpBatchUpsert | LdOpBatchUpdate | \
   LdOpBatchDelete | LdOpBatchMerge  | LdOpBatchQuery)



// -----------------------------------------------------------------------------
//
// ldOpToString - short human-readable name (for logs / tracing)
//
extern const char* ldOpToString(LdOp op);



// -----------------------------------------------------------------------------
//
// ldOpFromName - resolve an NGSI-LD op name to its LdOp bit
//
// Returns LdOpNone (0) when the name doesn't match any atomic op.
// Used by the CSR operations[] parser and by legacy call sites that
// still pass a string.
//
extern LdOp ldOpFromName(const char* name);



// -----------------------------------------------------------------------------
//
// ldOpGroupMaskFromName - resolve a § 4.20 group name to its OR-mask
//
// Returns 0 when the name is not a known group. Used alongside
// ldOpFromName by the CSR operations[] parser — entries that aren't
// atomic ops fall through to this lookup.
//
extern uint64_t ldOpGroupMaskFromName(const char* name);

#endif
