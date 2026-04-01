//
// FILE            LdOp.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// 
//
#ifndef LD_OP_H
#define LD_OP_H



// -----------------------------------------------------------------------------
//
// LdOp -
//
typedef enum LdOp
{
  LdOpNone = 0,

  // Entity ops
  LdOpCreateEntity,
  LdOpUpdateEntity,
  LdOpAppendAttrs,
  LdOpMergeEntity,
  LdOpReplaceEntity,
  LdOpDeleteEntity,
  LdOpDeleteAttr,
  LdOpReplaceAttr,

  // Batch ops
  LdOpBatchCreate,
  LdOpBatchUpsert,
  LdOpBatchUpdate,
  LdOpBatchDelete,
  LdOpBatchMerge,

  // Subscription ops
  LdOpCreateSubscription,
  LdOpUpdateSubscription,

  // Registration ops
  LdOpCreateRegistration,
  LdOpUpdateRegistration,

  // Retrieve ops
  LdOpRetrieveEntity,
  LdOpQueryEntities
} LdOp;

#endif
