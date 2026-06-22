# swNgsild — NGSI-LD Validation & Format Conversion

The NGSI-LD engine of the swBroker context broker: payload validation, the
normalized/concise/simplified format conversions, the query/scope/geo expression
languages, the subscription & registration caches, entity merge/replace logic, and
the distributed-operation (context-source) machinery. It sits on top of
[`swRest`](../swRest) (transport) and [`swJsonld`](../swJsonld) (context handling)
and targets **ETSI GS CIM 009 (NGSI-LD)**.

- **Version:** post-0.2.0
- **Language:** C
- **License:** Copyright 2026 Seamware

> swNgsild is a large library. This README is an orientation map — the feature
> areas and their main entry points — not an exhaustive function list. Each area's
> header (`Ld*.h` / `ld*.h`) carries the detailed contract.

## Architecture

The umbrella header exposes a thread-local per-request state struct, `SwNgsild`
(see `SwNgsild.h`), holding the parsed URL parameters and their derived forms
(`id`/`idV`, `type`/`typeExpr`, `q`/`qExpr`, `scopeQ`/`scopeExpr`, `pick`/`pickTree`,
`datasetId`, …). Service routines populate and read this state; the rest of the
library is the set of operations performed against it.

```c
#include "swNgsild/swNgsild.h"
```

## Feature areas

### Validation (`LdCheck.h`, `ldCheck*.h`)

Structural and semantic validation of incoming payloads:
`ldCheckEntity`, `ldCheckAttribute`, `ldCheckSubscription`, `ldCheckRegistration`,
`ldCheckGeo` (GeoJSON / geo-properties), `ldCheckDateTime` (ISO 8601),
`ldCheckUri`. Attribute-type detection lives in `ldAttrTypeDetect` /
`LdAttrType.h`. Problems are reported via `LdProblem.h` / `ldError.h` as
NGSI-LD ProblemDetails.

### Format conversion

NGSI-LD's three representations and the projections over them:
- **normalized / concise / simplified** — `ldRender`, `ldSimplifyEntity`,
  `LdFormat.h`, `ldEntityToApi` / `ldApiEntityToDbModel` (API ↔ DB model).
- **GeoJSON** output — `ldToGeoJson`.
- **LanguageProperty reduction** — `ldLangReduce`.
- **temporal / aggregated values** — `ldToTemporalValues`, `ldToAggregatedValues`.
- **pick / omit projection** (§ 4.21) — `ldPickOmit`, `LdProj.h`.

### Expression languages

Parsers + matchers/renderers for the NGSI-LD filter languages:
- **q** query language — `LdQ.h`, `ldQParse`, `ldQRender`, `ldQAttrs`.
- **scopeQ** — `LdScopeExpr.h`, `ldScopeMatch`.
- **type selection** (OR-of-AND) — `LdTypeExpr.h`.
- **geo-relations** — `LdGeoRel.h`.

### In-memory caches

- **Subscriptions** — `LdSubCache.h` (matching engine, status, counters).
- **Registrations** — `LdRegCache.h`.
- **Periodic notifications** — `LdPernotCache.h` / `ldPernotLoop`.
- **Snapshots** — `LdSnapshotCache.h`.
- **EntityMap** — `LdEntityMap.h` (frozen pagination over distributed queries).

### Entity operations

Merge, fragment and attribute-set logic shared by the write endpoints:
`ldEntityMerge` (RFC 7396-style merge with the NGSI-LD null marker),
`ldEntityAttrsSet`, `ldEntityFragment`, `ldEntityMatch`, `ldDatasetIdDedup`.

### Distributed operations & forwarding

Context-source registration dispatch and result assembly:
`ldDistOp`, `ldForwarding` / `LdForwarding.h`, `ldDistMerge`, `ldDistSub`,
`ldDiscovery` / `ldDiscoveryForward`, plus `LdOp.h` (the atomic-operation bitmap)
and context-source aliasing / loop detection (`ldCsourceAlias`).

### Notifications

`ldSubscriptionNotify`, `ldMqttNotify`, `ldNotifyDefer` (post-response delivery),
subscription counters and stats flushing (`ldSubStatsFlush`, `ldStatsFlushLoop`).

### Lifecycle

`ldInit` initializes the library; trace levels are in `ldTraceLevels.h`.

## Building

```bash
make            # build libswNgsild.a (+ .so)
make debug      # debug build
make ci         # clean + install
make di         # debug + install
```

`libswNgsild.a` links statically into its consumers. Sibling repos must be present
(the build references `../<lib>/lib<lib>.a`).

## Dependencies

Sibling sw-lib repos:

- [`swRest`](../swRest) — REST server + HTTP client (transport)
- [`swJsonld`](../swJsonld) — JSON-LD context expansion / compaction

Sibling k-lib repos:

- [`kalloc`](../kalloc) — arena allocator (`KAlloc`)
- [`kjson`](../kjson) — JSON parsing / trees (`KjNode`)
- [`kbase`](../kbase) — core utilities
- [`klog`](../klog) — logging
- [`ktrace`](../ktrace) — trace levels
- [`khash`](../khash) — hash tables (cache indexes)

Plus `pthread`.
