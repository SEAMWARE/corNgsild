//
// FILE            ldContextHost.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef LD_CONTEXT_HOST_H
#define LD_CONTEXT_HOST_H

#include "kjson/KjNode.h"                              // KjNode
#include "swJsonld/SwldContext.h"                      // SwldContext



// -----------------------------------------------------------------------------
//
// LD_VOLATILE_CTX_TTL_SEC - reap deadline for a never-fetched volatile context
//
// A volatile context is normally dropped on its first GET. This is only the
// backstop for the ones nobody ever dereferences: a consumer that needs the
// context fetches it right away, so an hour is generous.
//
#define LD_VOLATILE_CTX_TTL_SEC  3600



// -----------------------------------------------------------------------------
//
// ldContextHostVolatile -
//
// Host an inline / multi-element user @context as a one-shot served context
// so a Link header (response or distop forward) can reference it by URL. The
// returned SwldContext has ->url set to the served URL and carries the full
// expansion / compaction maps (so the same pointer drives swldCompactTreeWith
// and the Link header consistently). Never persisted to the DB; served with
// Cache-Control: no-store, dropped after the first GET, reaped at expiresAt
// if never fetched.
//
// ctxBody is the in-body @context node (object or array) — typically
// swNgsild.userContextBody. Returns NULL on failure (caller falls back to
// the core context).
//
extern SwldContext* ldContextHostVolatile(KjNode* ctxBody);



// -----------------------------------------------------------------------------
//
// ldContextHostReaperStart - register the volatile-context reap tick
//
// Must be called before ldPeriodicLoopStart().
//
extern void ldContextHostReaperStart(void);

#endif
