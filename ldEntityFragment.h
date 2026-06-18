#ifndef SWNGSILD_LDENTITYFRAGMENT_H_
#define SWNGSILD_LDENTITYFRAGMENT_H_

//
// FILE            ldEntityFragment.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Helper for write-op distributed dispatch: build a sub-entity fragment
// containing only the attributes claimed by one RegistrationInfo (§ 5.2.10).
//
// Used by the create/update/replace/merge/delete service routines to split
// an incoming entity payload along registration boundaries per § 4.3.6 /
// § 5.6. The fragment is POSTed / PATCHed to the registered Context
// Source; the remainder flows through the next dispatch pass or the
// local store.
//
#include <stdbool.h>                                   // bool

#include "kjson/KjNode.h"                              // KjNode, Kjson
#include "swNgsild/LdRegCache.h"                       // LdRegInfo



// -----------------------------------------------------------------------------
//
// ldEntityFragmentForInfo - build a fragment of entityP for one RegistrationInfo
//
// Returns an object containing id, type, @context (if present in entityP) and
// every top-level attribute that matches riP. Attribute matching:
//   - riP has no attributeNamesV → wildcard: every attribute of the entity
//     is claimed (only the id / type / @context keywords are skipped).
//   - Otherwise an entity attribute is claimed iff its name is in
//     riP->attributeNamesV (already expanded).
//
// If no attrs match, returns NULL — nothing to forward.
//
// detach == true:
//   Claimed attrs are moved (kjChildRemove + kjChildAdd) from entityP into
//   the fragment. After the call, entityP no longer holds them. Use for
//   exclusive / redirect passes where the broker must not keep or re-forward
//   the chopped attrs.
//
// detach == false:
//   Claimed attrs are shallow-linked into the fragment without being
//   removed from entityP (the fragment borrows KjNodes from entityP).
//   Use for inclusive where the local broker keeps the attrs AND forwards.
//   Caller must not mutate the fragment's children, and the fragment's
//   lifetime must not outlive entityP.
//
// id / type / @context are always cloned — they appear in every fragment
// but must stay on entityP too.
//
extern KjNode* ldEntityFragmentForInfo(KjNode*     entityP,
                                       LdRegInfo*  riP,
                                       Kjson*      kjP,
                                       bool        detach);

#endif  // SWNGSILD_LDENTITYFRAGMENT_H_
