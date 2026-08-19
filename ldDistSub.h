#ifndef CORNGSILD_LDDISTSUB_H_
#define CORNGSILD_LDDISTSUB_H_

//
// FILE            ldDistSub.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Distributed subscription fan-out (NGSI-LD § 5.8.1.4).
//
// When a local subscription is created whose entity-filter overlaps a
// registered Context Source, the broker forwards a derived sub to that
// source so entity changes "over there" reach our subscriber. The
// (parent → remote) mapping is recorded on LdSubCacheItem.subordinateP
// so later PATCH / DELETE / cleanup can be propagated.
//
// Loop prevention reuses the dist-op Via / csourceAlias machinery — the
// derived sub goes out with our own alias appended, so a remote that has
// us in its own reg-cache won't forward back to us.
//
#include "kjson/KjNode.h"                              // KjNode
#include "corNgsild/LdSubCache.h"                       // LdSubCache, LdSubCacheItem
#include "corNgsild/LdRegCache.h"                       // LdRegCache



// -----------------------------------------------------------------------------
//
// LdDistSubPersistFunc - callback to persist a sub's subordinate list
//
// Invoked by the helpers below whenever an LdSubCacheItem.subordinateP
// list is mutated. The implementation lives in coraine (where db.* is
// reachable); corNgsild just calls back when the in-memory state changes.
//
// userData is the cookie the helpers carry through.
//
typedef void (*LdDistSubPersistFunc)(LdSubCacheItem* itemP, void* userData);



// -----------------------------------------------------------------------------
//
// ldDistSubSubordinatesFragment - build {_subordinates, _subordinateRunNo}
//
// Returns a KjObject in the supplied Kjson buffer that, when applied as a
// JSON-merge-patch via db.subscriptionUpdate, replaces the sub doc's
// subordinate list with the current in-memory state. Suitable for direct
// hand-off to the persist callback.
//
struct Kjson;
extern KjNode* ldDistSubSubordinatesFragment(LdSubCacheItem* itemP, struct Kjson* kjsonP);



// -----------------------------------------------------------------------------
//
// ldDistSubFanout - forward a freshly-cached local sub to every matching CSR
//
// itemP      - the local LdSubCacheItem just added to the tenant's subCacheP.
//              Subordinate mappings are appended in-place to itemP->subordinateP.
// regCacheP  - tenant's CSR cache (NULL → no-op).
// ownAlias   - tenant's contextSourceAlias (used for Via header + self-loop check).
//
// Returns the number of derived subs successfully created.
//
// Match scope (initial cut, deliberately narrow):
//   * Only the sub's entitySelectors with a `type` are considered.
//   * A registration matches if any of its information[].entities[] entries
//     declares the same type. Reg-side id / idPattern narrowing is not yet
//     enforced; the derived sub carries the full local filter unchanged.
//   * CSR must declare LdOpCreateSubscription in its operations[] (the
//     federation default — type queries only — does not include it).
//
extern int ldDistSubFanout(LdSubCacheItem*      itemP,
                           LdRegCache*          regCacheP,
                           const char*          ownAlias,
                           LdDistSubPersistFunc persistFunc,
                           void*                persistUserData);



// -----------------------------------------------------------------------------
//
// ldDistSubCascadeDelete - propagate a local-sub DELETE to every derivative
//
// Walks itemP->subordinateP and sends
// DELETE <csr-endpoint>/ngsi-ld/v1/subscriptions/<remoteSubId> for each
// entry. Failures are logged and skipped — the local delete continues.
// Caller invokes this before freeing the cache item so the mapping is
// still reachable.
//
// regCacheP resolves regId → endpoint / Via-alias / contextSourceInfo /
// tenant via the existing dist-op send path. Subordinates whose CSR is
// no longer in the cache (e.g. removed since fanout) are skipped.
//
// Returns the number of remote DELETEs that came back 2xx.
//
extern int ldDistSubCascadeDelete(LdSubCacheItem* itemP,
                                  LdRegCache*     regCacheP,
                                  const char*     ownAlias);

// (no persist callback — the local sub doc is being deleted whole)



// -----------------------------------------------------------------------------
//
// ldDistSubReconcile - propagate a local-sub PATCH and reconcile overlap
//
// fragmentP is the JSON-merge-patch body received on PATCH /subscriptions
// (post-parseHook, i.e. expanded). The helper walks both the existing
// subordinate list and the reg cache and decides per CSR:
//
//   * still matches (after patch) and was already subordinated → PATCH
//     the remote with the merge fragment
//   * no longer matches (or CSR is gone) → DELETE the remote +
//     unlink the local subordinate entry
//   * newly matches and was not subordinated → fanout (POST) a new
//     derivative
//
// Returns the number of mutations applied (PATCHes + DELETEs +
// fanouts that came back 2xx). persistFunc fires once if the
// subordinate list changed.
//
extern int ldDistSubReconcile(LdSubCacheItem*      itemP,
                              KjNode*              fragmentP,
                              LdRegCache*          regCacheP,
                              const char*          ownAlias,
                              LdDistSubPersistFunc persistFunc,
                              void*                persistUserData);



// -----------------------------------------------------------------------------
//
// ldDistSubOnRegCreate - fan out derived subs for a freshly-added CSR
//
// Walks subCacheP for entity-subs whose entity-filter overlaps the new
// reg, and POSTs a derived sub to the CSR for each (subject to the same
// filters as ldDistSubFanout: CSR supports CreateSubscription, not a
// Via loop, no pre-existing subordinate for this regId).
//
// Called from postCsourceRegistration's service routine after the reg
// is added to regCacheP; analogue of ldCsrSubOnRegCreate for the
// entity-sub side.
//
extern int ldDistSubOnRegCreate(LdSubCache*          subCacheP,
                                LdRegCacheItem*      regItemP,
                                const char*          ownAlias,
                                LdDistSubPersistFunc persistFunc,
                                void*                persistUserData);



// -----------------------------------------------------------------------------
//
// ldDistSubOnRegDelete - clean up subordinates whose CSR is being removed
//
// Walks subCacheP and for each entity-sub, drops every subordinate
// entry pointing at regId. Best-effort DELETE is sent to the remote
// for each so the orphan goes away there too — failures are logged
// and the local mapping is unlinked regardless.
//
// Called from deleteCsourceRegistration's service routine BEFORE the
// reg is removed from regCacheP — the helper needs the live reg
// item to build the DELETE URL and pass through Via / tenant.
//
extern int ldDistSubOnRegDelete(LdSubCache*          subCacheP,
                                LdRegCacheItem*      regItemP,
                                const char*          ownAlias,
                                LdDistSubPersistFunc persistFunc,
                                void*                persistUserData);

#endif  // CORNGSILD_LDDISTSUB_H_
