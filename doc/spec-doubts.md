# NGSI-LD Spec Doubts & Gaps

Running list of ambiguities, silent cases, and genuinely contradictory
wording encountered while implementing swBroker / swNgsild against NGSI-LD.
Clause references follow the current ETSI specification, which is split
across two documents:

- **TS 104 175** — NGSI-LD Core API (refs prefixed `TS 104-175 §`)
- **TS 104 176** — NGSI-LD HTTP Binding (refs prefixed `TS 104-176 §`)

These supersede the single GS CIM 009 v1.9.1, against which these doubts
were originally raised. Intended as input for ETSI TC DATA clarifications.

Each entry: **§ ref** · **what we hit** · **what the spec says (or
doesn't)** · **what we did** · **what we'd want fixed**.

---

## Index — by severity of needed fix

The fix the spec needs is not the same for every entry. Three
categories matter for an improvement-suggestion document:

- **Errors** — spec wording is provably wrong, contradicts itself,
  or every implementation has to ignore it to ship.
- **Missing** — spec doesn't address the case at all; an entire
  shape or mechanism needs to be added.
- **Ambiguities** — spec is silent or unclear; multiple readings
  are equally defensible.

The themed cross-reference further down is by NGSI-LD mechanism
(distops, CSR, subscriptions, …) for navigation purposes.

### Errors (spec is *wrong*)

- **[8](#8)** TS 104-175 § 5.2.6.5.3 — typos in top-level CSR fields are silently re-classified as CSF Properties. Silent data drop is the wrong behaviour;
                        a typo at registration time must be rejected.
- **[14](#14)** TS 104-176 § 11.2.3.1 — GET /entityMaps **creates** an EntityMap (returns 201). REST contract violation;
                             mismatched with HTTP method semantics every tool assumes.

### Missing (spec *doesn't address* this case at all)

- **[1](#1)** TS 104-175 § 10.2.2.4 — createEntity partial-success body has no shape defined. Same gap recurs for append / update / merge / delete entity ops.
- **[11](#11)** TS 104-175 § 10.6.2 / TS 104-176 § 7.15–7.18 — no URL param for "local + registry, no forward". A real use case with no spec-blessed way to ask for it.
- **[12](#12)** TS 104-175 § 10.6.2 — federation has no hop / TTL bound. Loop detection is the only stop; deep federation without cycles can still chain indefinitely.
- **[13](#13)** TS 104-175 § 10.2 — no Retrieve Attribute operation. Clients must GET the whole entity and project; no minimal endpoint.
- **[32](#32)** TS 104-175 § 5.2.6.4.4 — ProblemDetails extension fields for NGSI-LD-specific context (entityId, attributeName, registrationId, status) are vendor-invented today. No blessed set.
- **[47](#47)** TS 104-176 § 6.3.5 — `NGSILD-Warning` header is defined but emission semantics are missing. No broker emits them today because the spec doesn't say WHEN to.
- **[59](#59)** TS 104-175 § 5.2.4 — system Attributes set and visibility gate not enumerated in one place. Inferred from scattered references.
- **[62](#62)** TS 104-175 § 13 — `jsonldContext.kind` values (Cached / ImplicitlyCreated / Hosted / ExplicitlyCreated) not enumerated; provenance vocabulary not standardised.
- **[80](#80)** TS 104-175 § 5.3.2.4 — simplified-form projection table for List / Vocab / Json / Geo / Language attribute types missing. Implementer reconstructs from scattered hints.
- **[83](#83)** TS 104-175 § 7.2.6 — temporal bound asymmetry (before exclusive, after inclusive). No clean way to express "at or before T". Design gap, not a contradiction.
- **[84](#84)** TS 104-175 § 7.6 — what to do with `?orderBy=` under distop. Spec says sort is not applied; doesn't say whether to ignore silently, warn, or 400 the request. And no recommended default order.
- **[86](#86)** TS 104-176 § 6.4.9 + TS 104-175 § 16 — `NGSILD-Snapshot:` is documented only for reads in 1.9.1. The same header against the normal CRUD endpoints (POST /entities + header, PATCH, DELETE, …) would naturally address the snapshot's tenant, but the spec neither blesses nor forbids it. Future revisions will define snapshot writes via the existing CRUD API + header; today's spec needs an explicit "writes against NGSILD-Snapshot are not defined in this revision; brokers SHALL reject with 405 / 501" or equivalent, to make the version transition unambiguous.
- **[87](#87)** TS 104-175 § 5.2.6.5.2 / TS 104-175 § 10.5 — Subscription has no `datasetId` filter for multi-instance attributes. `watchedAttributes` is attribute-level only, with no way to subscribe to "this specific dataset instance". Critical for streaming-action / per-job state-machine use cases where each in-flight goal/job is its own datasetId.
- **[88](#88)** TS 104-175 § 10.5.7 — Subscription trigger condition `AttributeInstanceCreate` (and siblings: `Update`, `Delete`) for multi-instance attributes. The current Add / Update / Delete trigger taxonomy is whole-attribute granularity; appearance of a new dataset instance on an existing attribute is invisible as a distinct event.

### Ambiguities (spec is *silent* or *unclear*)

The remaining **68 entries** fall into this category. They are
best browsed via the cross-reference index below — grouped by
NGSI-LD mechanism so a reviewer focused on (say) distributed
operations can locate the relevant subset quickly.

---

## Index — by NGSI-LD mechanism (cross-reference for navigation)

### A. Distributed operations — concurrent fan-out & policy

- **[27](#27)** TS 104-175 § 9.1 — concurrent vs sequential forwarding to matching CSRs is unspecified (no permission, no obligation, no constraint)
- **[28](#28)** TS 104-175 § 5.2.6.6.6 — per-CSR `timeoutMs` semantics when N CSRs forward concurrently
- **[29](#29)** TS 104-175 § 9.5.1 — outbound header policy on forwarded requests is half-defined (Via, NGSILD-Tenant, Content-Type, Accept, Link, Authorization, X-Forwarded-For, inbound user headers — no normative table)
- **[36](#36)** TS 104-175 § 5.2.6.4.8 — distop counter (timesSent/Failed/Success/Failure) atomicity under concurrent fan-out and HA replicas sharing a CSR store
- **[31](#31)** TS 104-175 § 14.5.4 — multi-CSR EntityMap aggregation (linkedMaps shape, dedup rule, multi-source array semantics)
- **[35](#35)** TS 104-175 § 10.4.2.4 — auxiliary mode merge when two aux CSRs overlap concurrently (first-wins? timestamp? reject at creation?)
- **[46](#46)** TS 104-176 § 6.3.5 — "redirect" mode classified as BOTH single-source and multi-source in the same section (contradiction)
- **[47](#47)** TS 104-176 § 6.3.5 — `NGSILD-Warning` header emission semantics are underspecified (no broker emits them today; 4 codes share triggers)
- **[77](#77)** TS 104-175 § 9.4 — Via parsing corner cases (case, whitespace, depth limit, malformed pseudonym)

### B. Distributed operations — composition & routing

- **[1](#1)** TS 104-175 § 10.2.2.4 — createEntity partial-success body format (no shape defined; same gap for append / update / merge / delete)
- **[30](#30)** TS 104-175 § 10.4.3.4 — split-mode forwarded query: which URL params get stripped vs survive vs narrow per CSR (no normative list)
- **[34](#34)** TS 104-175 § 10.2.9 + TS 104-175 § 8.5.3 — Merge Entity composition order when applying across local + N CSRs in parallel
- **[39](#39)** TS 104-175 § 10.2 (generic) — collapsing uniform-error multi-CSR responses (207 noise vs single ProblemDetails)
- **[37](#37)** TS 104-175 § 10.2.4.4 — `?options=noOverwrite` semantics when the same attr exists locally AND on an exclusive CSR
- **[38](#38)** TS 104-175 § 10.2.7 — `?deleteAll=true` forwarded to a CSR that doesn't support `deleteAttrs` (refuse all? skip CSR? partial?)
- **[2](#2)** TS 104-175 § 10.2.2.4 + TS 104-175 § 9.3.3 — exclusive CSR without `createEntity` op: deadlock semantics for fully-claimed input
- **[3](#3)** TS 104-175 § 10.2.2.4 — entity shell when all attributes are claimed externally (create `{id, type}` locally or not?)
- **[12](#12)** TS 104-175 § 10.6.2 — federation has no hop / TTL bound; loop detection is the only stop
- **[51](#51)** TS 104-175 § 7.4 / TS 104-176 § 6.4.7 — `limit` / `offset` semantics under distributed federation (per-CSR vs global; offset composition)
- **[64](#64)** TS 104-176 § 6.4.6 — `count` semantics under distributed federation (approximate vs exact; per-CSR count-only forward)
- **[78](#78)** TS 104-175 § 8.6 — `?local=true` behaviour matrix across read/write/sub/info endpoints
- **[79](#79)** TS 104-175 § 8.5.2 — batch with two elements addressing the same entityId (merge? sequence? reject?)

### C. Response shapes & error formats

- **[32](#32)** TS 104-175 § 5.2.6.4.4 — ProblemDetails extension fields for NGSI-LD-specific context (`entityId`, `entityIds`, `attributeName`, `registrationId`, `status`)
- **[23](#23)** TS 104-176 § 6.4.7 — 206 Partial Content vs 200 OK on temporal queries; ETSI tests are mutually inconsistent
- **[19](#19)** TS 104-175 § 8.2.3 / TS 104-175 § 8.2.2 — InvalidRequest vs BadRequestData for invalid URL-param values
- **[20](#20)** TS 104-175 § 10.5.7 — `attributeDeleted` notification representation: bare-string default vs ETSI-fixture extended-form
- **[50](#50)** TS 104-176 § 6.4.2 / TS 104-175 § 5.3.3.3 — GeoJSON negotiation: Accept header vs `?format=geojson` URL param (and the geometryProperty axis)

### D. CSR data model

- **[4](#4)** TS 104-175 § 5.2.6.5.3 — `endpoint` URL convention: host+port (example) vs full API base (silent normative)
- **[5](#5)** TS 104-175 § 10.6.3 + TS 104-175 § 5.2.6.5.3 — tenant-scoped loop detection alias (per-tenant or per-broker?)
- **[7](#7)** TS 104-175 § 5.2.6.5.3 — CSR is over-nested and hard to write correctly (frequent user mistake → 422 / vendor-specific salvage)
- **[8](#8)** TS 104-175 § 5.2.6.5.3 — typos in top-level CSR fields silently accepted as CSF Properties
- **[9](#9)** TS 104-175 § 9.5.1 / 4.3.6.6 — contextSourceInfo edge cases (well-known keys, banned keys, `urn:ngsi-ld:request` substitution)
- **[10](#10)** TS 104-175 § 5.2.6.5.3 — location / observationSpace / operationSpace match semantics (containment? overlap?)
- **[45](#45)** TS 104-175 § 5.2.6.5.3 — CSR `location` shape: bare GeoJSON Geometry (spec) vs wrapped GeoProperty (ETSI fixture)
- **[71](#71)** TS 104-175 § 5.2.6.5.6 — `contextSourceAlias` uniqueness scope (broker-local? federation? historical?) and allocation authority
- **[72](#72)** TS 104-175 § 5.2.6.5.6 — `contextSourceExtras` opaque-JSON contract (same gap as JsonProperty, entry 43)
- **[82](#82)** TS 104-175 § 9.2 — CSR `operations` list: named-group + literal-op mixing, typos, empty array vs absent default
- **[81](#81)** TS 104-175 § 15.2.4 / TS 104-175 § 5.2.6.5.6 — `/info/sourceIdentity` per-tenant field variation (which fields vary; uptime semantics)

### E. Discovery & federation

- **[6](#6)** TS 104-175 § 12.3.3 — Discovery as a separate registry process (vs the implicit broker-driven matching)
- **[26](#26)** TS 104-175 § 12.3.3.5 — RegistrationInfo filtering on GET /csourceRegistrations is "should", not "shall"; per-request opt-in lacking
- **[11](#11)** TS 104-175 § 10.6.2 / TS 104-176 § 7.15–7.18 — no URL param for "local + registry, no forward" use case

### F. Local operations & data semantics

- **[33](#33)** TS 104-175 § 10.2.10 — Replace Entity: can the type change locally? Spec is permissive, implications unspecified
- **[13](#13)** TS 104-175 § 10.2 — no Retrieve Attribute operation (forces clients to do GET /entity + project)
- **[14](#14)** TS 104-176 § 11.2.3.1 — GET /entityMaps creates (HTTP method semantics violate REST norms)
- **[22](#22)** TS 104-175 § 10.2.7 — `?deleteAll=` boolean parsing case-sensitivity
- **[18](#18)** TS 104-175 § 16.2.4 — snapshot capture from CSRs in no-split deployments
- **[44](#44)** TS 104-175 § 10.2.4.4 — `?observedAt=<time>` default-observedAt URL param exists for Merge but not Append/Update
- **[63](#63)** TS 104-175 § 14 — EntityMap lifetime, eviction, and persistence across restart
- **[65](#65)** TS 104-175 § 16.2.4 — snapshot capture from CSRs: do CSR-side changes after capture invalidate the snapshot? (frozen vs live)
- **[67](#67)** TS 104-175 § 11.3.2 — `lastN` semantics across pagination and distop fan-out
- **[69](#69)** TS 104-175 § 10.2.12 — Purge Entities and TRoE interaction (current-state only, or both?)
- **[76](#76)** TS 104-176 § 6.4.9 — `NGSILD-Snapshot` header: distop forwarding, subscription scope, mixed-mode interaction

### G. Notifications & subscriptions

- **[21](#21)** TS 104-175 § 5.2.6.5.2 — entityDeleted vs attributeDeleted trigger symmetry on entity delete
- **[16](#16)** TS 104-175 § 9.5.2 / TS 104-175 § 10.5 — `jsonldContext` fallback asymmetry between subscriptions and CSRs
- **[17](#17)** TS 104-176 § 6.5.2 / TS 104-176 § 6.5.3 — `urn:ngsi-ld:request` substitution for receiverInfo
- **[52](#52)** TS 104-175 § 10.5.7 — distributed-subscription "except for the one received from" lacks identification mechanism (Via? regId? source URI?)
- **[53](#53)** TS 104-175 § 10.5.7 — change detection includes server-stamped `modifiedAt` (literal reading triggers notification storms on idempotent overwrites)
- **[54](#54)** TS 104-175 § 10.5.7 — `notification.attributes` IRI expansion timing: subscription-creation vs notification-time context
- **[55](#55)** TS 104-175 § 10.5.7 — delete-trigger notification + `notification.attributes` filter (which wins?)
- **[56](#56)** TS 104-175 § 10.5.7 — `notification.pick` / `notification.omit` vs entity-keyword members (can `id` be omitted?)
- **[57](#57)** TS 104-175 § 10.5.2.4 / TS 104-175 § 10.5.3.4 — overlapping subscriptions to the same endpoint: dedup or duplicate?
- **[58](#58)** TS 104-175 § 10.5 — subscription `expiresAt` reached while broker is offline (recovery state machine undefined)
- **[68](#68)** TS 104-175 § 12.4 — csr-subscription notification triggers: which CSR state changes count (CRUD only? counter updates?)
- **[73](#73)** TS 104-175 § 5.2.6.6.1 — notification `cooldown` state machine: failure definition, scope, re-engagement, observability
- **[87](#87)** TS 104-175 § 5.2.6.5.2 / TS 104-175 § 10.5 — Subscription has no `datasetId` filter; `watchedAttributes` is attribute-level only — coarse for multi-instance use cases (streaming actions, per-job state machines, per-sensor channels)
- **[88](#88)** TS 104-175 § 10.5.7 — Add `AttributeInstanceCreate` / `AttributeInstanceUpdate` / `AttributeInstanceDelete` trigger conditions so subscribers can distinguish per-instance lifecycle events from whole-attribute Add/Update/Delete

### H. JSON-LD / @context

- **[15](#15)** TS 104-176 § 6.2.4 — @context placement for array bodies is ambiguous (per-root vs per-element)
- **[43](#43)** TS 104-175 § 5.2.6.4.10 — JsonProperty inner-value @context interaction: where does opaqueness start and end?
- **[62](#62)** TS 104-175 § 13 — `jsonldContext.kind` values not enumerated (Cached / ImplicitlyCreated / Hosted / ExplicitlyCreated)

### I. Data model — multi-instance, linked entities, intake order

- **[40](#40)** TS 104-175 § 7.4.2.2 — chronological order on batch arrays is an unenforceable assumption (no mechanism to assert or signal intent)
- **[41](#41)** TS 104-175 § 8.5 — multi-instance with identical datasetId on intake (reject? dedupe? defer?)
- **[42](#42)** TS 104-175 § 7.7 — `joinLevel` semantics with cycles, diamonds, and originating-id self-reference
- **[48](#48)** TS 104-175 § 8.5.3 — "indeterminate / random" tiebreaker on identical datasetIds is unfit for cross-broker interop
- **[49](#49)** TS 104-175 § 8.5.1 — `datasetId: "@none"` round-trip drops the field on response (POST body not reproducible from GET)
- **[59](#59)** TS 104-175 § 5.2.4 — system Attributes set and visibility gate not enumerated in one place (sysAttrs scope, observedAt vs gate)
- **[60](#60)** TS 104-175 § 7.3.2 — `pick` / `omit` / `attrs` cross-parameter validation matrix missing (combination rules, conflict resolution)
- **[61](#61)** TS 104-175 § 5.2.6.9 — query language `q` lacks precedence table, escaping rules, type coercion, null-matching semantics
- **[66](#66)** TS 104-175 § 8.2.4 — IRI expansion of terms not defined in active @context (reject? @vocab fallback? bare?)
- **[70](#70)** TS 104-175 § 8.4.3 — `urn:ngsi-ld:null` tombstone scope: attribute-level only, or also inside array values / opaque json?
- **[74](#74)** TS 104-175 § 5.2.6.4.6 — LanguageProperty simplified rendering without `?lang=` URL param (lossless? default language? 400?)
- **[75](#75)** TS 104-175 § 5.3.2.7 — aggregated temporal × `?sysAttrs=true` interaction (per-period? attribute-level? suppressed?)
- **[80](#80)** TS 104-175 § 5.3.2.4 / TS 104-175 § 5.2.6.4.8 / TS 104-175 § 5.2.6.4.9 — simplified-form projection table for List/Vocab/Json/Geo attribute types
- **[83](#83)** TS 104-175 § 7.2.6 — temporal bound asymmetry: `before` exclusive, `after` inclusive, `between` half-open — pick one policy
- **[84](#84)** TS 104-175 § 7.6 — "Sort ordering is never applied to distributed operations" — every implementer contradicts this; spec text needs replacement
- **[85](#85)** TS 104-175 § 7.6.2.2 — datatype comparison order: Strings before DateTime is non-intuitive; Geo/Vocab/Language/List/Json buckets unstated

---

<a name="1"></a>
## 1. TS 104-175 § 10.2.2.4 — createEntity partial success body format

**Hit:** When some forwards succeed and others fail, the spec says the
response is "a partial success". No body shape defined.

**Spec:** Only phrase is "a partial success if some parts of it succeeded".
TS 104-175 § 5.2.6.8.5 defines `UpdateResult` with `updated[]` / `notUpdated[]` for
attribute-update ops, but no equivalent for createEntity.

**Our call:** 207 Multi-Status with body
`{ "notCreated": [ NotUpdatedDetails ] }`, reusing TS 104-175 § 5.2.6.8.4 shape
(`attributeName`, `reason`, `registrationId?`). Success side implicit.

**Fix wanted:** Spec should define the partial-success body formally —
either extend `UpdateResult` semantics to createEntity, or introduce a
dedicated `CreateResult` type. Same gap appears for append/update/merge/
delete entity ops (TS 104-175 § 10.2.3–10.2.8).

---

<a name="2"></a>
## 2. TS 104-175 § 10.2.2.4 + TS 104-175 § 9.3.3 — exclusive CSR without createEntity op

**Hit:** A CSR with `mode: exclusive` and `operations: ["retrieveEntity"]`
(i.e. no `createEntity`) claims a set of Attributes on an Entity, but:

- Per TS 104-175 § 9.3.3, the broker cannot hold those Attributes locally (the
  exclusive invariant).
- The forwarded createEntity is refused by the registration.

If the admin creates such a reg, can those Attributes ever be created?

**Spec:** TS 104-175 § 10.2.2.4 says "for matching Registrations where the Create
Entity operation is not supported, this shall result in an error of
type Conflict ... or a partial success if some parts of it succeeded."

The spec treats this as a per-attribute failure, not a deadlock —
**implicitly** relying on the user having submitted other Attributes
that go elsewhere or stay local. If the input payload consists entirely
of exclusive-claimed Attributes with no createEntity support, the whole
create fails with Conflict and the Attributes can never be created.

**Our call:** Treat it as a legitimate "admin forbids creating these
Attributes" feature. Partial success (207) if other Attributes succeed;
409 Conflict if no other success.

**Fix wanted:** Spec should explicitly either (a) forbid creating such
a CSR at registration time, or (b) bless the "forbid-creation" semantics
so admins can rely on it, or (c) define a way out (e.g., override when
no other source accepts).

---

<a name="3"></a>
## 3. TS 104-175 § 10.2.2.4 — entity shell when all Attributes are claimed externally

**Hit:** POST /entities with attrs A, B. Exclusive/redirect CSRs claim
both. After chop, remaining input data = `{ id, type }`. Do we create
a local entity shell?

**Spec:** "Any remaining input data shall be used to create the Entity
locally." If remaining data is empty, behaviour is undefined.

**Our call:** Skip local create (no shell). A separate `db.entityRetrieve`
check catches the `AlreadyExists` case (if a local shell pre-existed from
earlier traffic) so we can still raise 409 when appropriate.

**Fix wanted:** Spec should clarify whether a local `{id, type}` shell
is created in this case. Two defensible positions: (i) yes, to record
entity existence for later retrieve-merge; (ii) no, to keep the broker
truly empty per the exclusive invariant. Pick one, document it.

---

<a name="4"></a>
## 4. TS 104-175 § 5.2.6.5.3 — CSR endpoint URL convention

**Hit:** Is `endpoint` the Context Source's base URL (host+port) with
the broker adding `/ngsi-ld/v1/...`, or is it the full API base path
(host+port+`/ngsi-ld/v1`)?

**Spec:** TS 104-175 § 5.2.6.5.3 defines `endpoint` as "URI dereferenceable to access
the registered Context Source". Annex C.3 example uses
`"endpoint": "http://my.csource.org:1026"` — host+port only, no API
path. So **the example implies the broker must prepend `/ngsi-ld/v1/...`**
when forwarding. But the normative text doesn't say this.

**Our call:** Follow the example. CSR endpoint is host+port (or any
prefix the admin wants); broker appends the standardised NGSI-LD path
(`/ngsi-ld/v1/entities`, `/ngsi-ld/v1/entities/{id}`, etc.).

**Fix wanted:** Spec should add a normative sentence: "The NGSI-LD
standardised path segments (e.g. `/ngsi-ld/v1/entities`) shall be
appended by the Context Broker when dereferencing the endpoint. The
endpoint value shall not include these segments."

---

<a name="5"></a>
## 5. TS 104-175 § 10.6.3 + TS 104-175 § 5.2.6.5.3 — tenant-scoped loop detection alias

**Hit:** Multi-tenant broker. The `Via` header alias for loop detection
must distinguish between the same broker serving different tenants,
otherwise a legitimate tenant-rewrite forward (CSR with `tenant: T2`,
endpoint back to the same broker) is falsely detected as a loop.

**Spec:** TS 104-175 § 10.6.3 defines `contextSourceAlias` for loop detection in
general. TS 104-175 § 5.2.6.5.3 defines `tenant` on a CSR to rewrite the target
tenancy. Neither section explicitly says the alias is per-tenant.

**Our call:** The alias we emit is `<base>:<tenant>` where `base` is
the broker's configured alias and `tenant` is the request's NGSILD-Tenant
(default tenant → bare `<base>`, no suffix). Loop detection compares
exact alias strings.

**Fix wanted:** Spec should state that when a broker serves multiple
tenants, the loop-detection alias shall be scoped per-tenant. Otherwise
a broker can never host `tenant: T2` rewrite CSRs to itself.

---

<a name="6"></a>
## 6. TS 104-175 § 12.3.3 Discovery — separate registry process

**Hit:** CSR Discovery (`GET /csourceRegistrations?...`) appears to be a
separate concern from a broker's own internal registration cache. The
filter language (TS 104-175 § 12.3.3) is richer than what a broker uses for dispatch.

**Spec:** TS 104-175 § 12.3 frames Discovery as a service a Context Registry
offers, but it's unclear whether a Context Broker must also implement
Discovery over its own regCache, or only a Context Registry does.

**Our call:** Broker returns 501 Not Implemented on the rich
Discovery query syntax; offers CRUD only over its own local regCache.

**Fix wanted:** Clarify the role separation: what must a Context Broker
implement vs a Context Registry? Can a broker advertise itself as "no
Discovery support" without breaking federation?

---

<a name="7"></a>
## 7. TS 104-175 § 5.2.6.5.3 — CSR is over-nested and hard to write correctly

**Hit:** Declaring a CSR requires two levels of array nesting to express
even the simplest "forward this entity type to that endpoint":

```
"information": [
  {
    "entities": [
      { "id": "urn:V1", "type": "Vehicle" }
    ],
    "propertyNames": ["speed"]
  }
]
```

The outer `information[]` groups RegistrationInfo entries (each with a
different attr set + entity pattern); the inner `entities[]` enumerates
EntityInfo entries sharing those attrs. Both arrays are commonly of
length 1, but the spec forces the array wrappers anyway.

**Spec:** TS 104-175 § 5.2.6.6.5 RegistrationInfo + TS 104-175 § 5.2.6.6.2 EntityInfo are defined
as cardinality `1..N` arrays even though the 1-element case is by far
the most common in practice.

**Our call:** Parse as specified. Every test we write pays the two-level
nesting tax even for trivial cases.

**Fix wanted:** Allow the singular shortcut at both levels — a bare
object in place of a 1-element array. JSON Schema / OpenAPI tooling
handles this pattern routinely; the spec could say "value may be a
RegistrationInfo object or an array of RegistrationInfo objects". Same
for `entities`. Reduces writer burden by ~6 lines per trivial CSR.

---

<a name="8"></a>
## 8. TS 104-175 § 5.2.6.5.3 — typos in top-level CSR fields are silently accepted as CSF Properties

**Hit:** A CSR has well-known top-level keys (`id`, `type`, `endpoint`,
`mode`, `information`, `operations`, ...). Anything else at the top
level is treated as a Context Source Property (CSF property —
user-defined metadata about the source).

This means a typo like `"ifnormation"` instead of `"information"` is
**silently accepted**: the typo'd key is stored as a CSF property, and
then validation fails because the mandatory `information` field is
missing. The error message points at the missing mandatory field, not
the typo — debugging is unnecessarily painful.

**Spec:** TS 104-175 § 5.2.6.5.3 table lists all standard fields; additional fields
are implicitly CSF properties via NGSI-LD's open-schema model. No
explicit JSON-LD frame, no `additionalProperties: false`, so typos
can never be caught structurally.

**Our call:** Same behaviour — typos become silent CSF properties.
Users get the confusing "missing mandatory information" error.

**Fix wanted:** Move user-defined CSR properties under a `properties`
umbrella — e.g.

```
{
  "id": "...",
  "type": "ContextSourceRegistration",
  "endpoint": "...",
  "information": [...],
  "properties": {
    "customKey1": "value1",
    "customKey2": "value2"
  }
}
```

With that structure, standard fields have a closed set and typos are
caught as "unknown field at top level". Same rethink applies to
Subscription (which has the same class of issue with notification
metadata) and to Entity-level spec shapes.

---

<a name="9"></a>
## 9. TS 104-175 § 9.5.1 / 4.3.6.6 — contextSourceInfo edge cases

**Hit:** Several under-specified edge cases around contextSourceInfo:

(a) **Banned keys at registration-creation time.** TS 104-175 § 9.5.1 says binding-
    level headers (e.g. `Content-Length`) "shall be ignored" at forward
    time. But the spec doesn't say whether a CSR with those keys should
    be **rejected at creation** (400 Bad Request) or **silently accepted
    and filtered later**. Same question for `NGSILD-Tenant` — the spec
    says it "shall not be part of contextSourceInfo", with no explicit
    rejection semantics.

(b) **Conflict between `contentType` CSI key and incoming Content-Type.**
    If the CSR declares `contentType: application/json` but the incoming
    request is `application/ld+json` (with payload containing `@context`),
    does the broker strip `@context` before forwarding? TS 104-175 § 9.5.2 only
    spells this out for the `jsonldContext` case.

(c) **`accept` on write ops.** TS 104-175 § 9.5.2 says `accept` applies to
    "response from the distributed endpoint". For createEntity a 201
    response is empty-body — does `accept` still get sent as a header?
    The spec doesn't differentiate write vs read ops.

(d) **`urn:ngsi-ld:request` sentinel scope.** TS 104-175 § 9.5.1 says a value of
    `urn:ngsi-ld:request` means "pull from triggering request", but only
    *some* headers can be meaningfully pulled (e.g. `Authorization`,
    not `Content-Length`). The spec doesn't list which headers are
    eligible.

(e) **Advanced semantics deferred.** `jsonldContext` (TS 104-175 § 9.5.2) requires
    the broker to perform JSON-LD compaction against a remote `@context`,
    switch `Content-Type` to `application/json`, and strip `@context`
    from the body. `ngsildConformance` requires a version-compat
    transform (defined in TS 104-175 § 9.5.3). Both are non-trivial.

**Our call (v1):**
- No creation-time rejection for banned keys → silently filter at forward
  time (implements the "shall be ignored" instruction literally).
- `accept` / `contentType` special-cased as HTTP `Accept` / `Content-Type`.
- `jsonldContext` / `ngsildConformance` / `urn:ngsi-ld:request` deferred.

**Fix wanted:** Spec should be prescriptive about (a) and (d); clarify
write-op behaviour of `accept` in (c); pin down the `contentType`/
`@context` interaction in (b).

---

<a name="10"></a>
## 10. TS 104-175 § 5.2.6.5.3 location / observationSpace / operationSpace — match semantics

**Hit:** CSR geo fields govern which forwards are eligible, but the spec
doesn't spell out the matching **relation** clearly.

**Spec:**
- `location` — "Geographic location that includes the **locations** of all
  entities for which the Context Source may be able to provide information."
- `observationSpace` — "includes the **observation spaces** of all
  entities as specified by their respective observationSpace GeoProperty".
- `operationSpace` — same shape, for operationSpace.

But no explicit `georel` — should the broker test "entity.location **within**
csr.location", or "intersects", or "contains"? The spec phrasing "includes"
suggests the CSR's geometry is an **outer boundary** and matching should
be **entity within CSR**, but it's inferred, not stated.

Likewise, `observationSpace` / `operationSpace` match entity-side attrs
rather than the entity's own location — adding asymmetry.

**Our call:** Implemented for createEntity dispatch. Routes through the
existing `db.geoMatchFunc` callback (shared `src/plugins/shared/geoMatch.c`),
so swNgsild stays GEOS-free. Applied semantics: entity's
`location`/`observationSpace`/`operationSpace` must be **within** the
corresponding CSR geometry. An entity lacking the corresponding
GeoProperty fails the match (scope-like behaviour — geo-restricted CSRs
don't apply to location-less entities).

**Fix wanted:** Spec should state the georel explicitly for each of the
three fields. Probably "entity's GeoProperty X must be within (or equal
to) CSR's X-Space" for all three.

---

<a name="11"></a>
## 11. TS 104-175 § 10.6.2 / TS 104-176 § 7.15–7.18 — no URL param for "local + registry, no forward"

**Hit:** Discovery endpoints (/types, /types/{type}, /attributes,
/attributes/{attrId}) have three natural modes per TS 104-175 § 10.6.2:

1. **Local only** — only the local datastore.
2. **Local + CSR metadata** — augment with types/attrs declared in
   `information.entities[].type`, `propertyNames`, `relationshipNames`.
3. **Local + CSR metadata + forwarded retrieval** — forward the GET
   to every CSR that supports the relevant retrieve op and merge.

The spec defines `?local=true` (mode 1) and text for TS 104-175 § 10.6.2 lays
out both "local + registry" and "forward" as alternatives (mode 2 and
mode 3), but the HTTP binding has **no URL parameter** to ask for
mode 2 specifically. The default is underspecified — implementations
will diverge on whether the default is mode 2 or mode 3.

**Spec:** TS 104-175 § 10.6.2 — "implementations may only take into account
information that is available or can be derived from a local datastore
**and the Context Registry** … **As an alternative** … the request
can be forwarded to all Context Sources". Reads as two implementation
options but without a client-side switch.

**Our call:** Added a non-standard URL param `?noForward=true` that
requests mode 2 (local + CSR metadata, no forwarding). Default (no
`?local` and no `?noForward`) is mode 3 (forward).

**Fix wanted:** A standardised URL param (suggested `noForward=true`)
so clients can explicitly request the middle mode, plus spec language
mandating what the default is when the param is absent.

---

<a name="12"></a>
## 12. TS 104-175 § 10.6.2 federation depth — no hop / TTL bound

**Hit:** Mode-3 discovery forwarding is recursive by nature: every
forwarded GET lands on a broker that itself re-forwards to its own
CSRs. In a misconfigured or deep federation this can explode. Via-
header loop detection catches **cycles** but not deep **chains**,
and for discovery (which asks each CSR to aggregate upstream) it
does not stop on legitimate non-cyclic trees that are simply too
deep.

**Spec:** Silent on depth. TS 104-175 § 10.6.2 describes forwarding as an option
without any bounded-traversal concern.

**Our call:** Added a non-standard URL param `?hops=<N>`. Decremented
on each outgoing forward; when the inbound hop count reaches 0 we
stop forwarding entirely. Default when absent: 8 — deep enough for
realistic federations while capping the worst case.

**Fix wanted:** Standardise the hop limit (name `hops` preferred over
`ttl`; TTL-in-HTTP is ambiguous vs. cache-age) with a clear default
and the rule "decrement on each hop, stop at 0".

---

<a name="13"></a>
## 13. TS 104-175 § 10.2 — no Retrieve Attribute operation

**Hit:** The spec has PATCH/DELETE/PUT on `/entities/{id}/attrs/{attrId}`
but no GET. Retrieving one specific attribute requires fetching the whole
entity and filtering client-side (or using `?pick=`, which still returns
the wrapping entity and its id/type).

**Spec:** TS 104-175 § 10.2 covers Partial Attribute Update (5.6.4), Delete Attribute
(5.6.5), Replace Attribute (5.6.19). There is no Retrieve Attribute
operation, and no corresponding CSR op name.

**Our call:** Implemented `GET /entities/{id}/attrs/{attrId}` locally,
returning the Attribute in NGSI-LD API format (plain Property /
Relationship / ... object, no entity wrapping). `?datasetId` filters
the default or specific instance. `?sysAttrs` preserved. DistOps
forwarding not wired — no agreed op name.

**Fix wanted:** Add a Retrieve Attribute operation to TS 104-175 § 10.2 with a
defined response shape (plain Attribute vs. minimal entity wrapper)
and a CSR op name (candidate: `retrieveAttribute`) so distributed
retrieval of a single attribute is specified the same way as
update / delete / replace are.

---

<a name="14"></a>
## 14. TS 104-176 § 11.2.3.1 — GET /entityMaps creates (HTTP method semantics)

**Hit:** TS 104-176 § 11.2.3.1 binds the HTTP GET method on `/entityMaps` to the
"Create EntityMap for Query Entities" operation, responds 201 Created,
and includes an `NGSILD-EntityMap` location header. The same semantics
apply to GET /temporal/entityMaps (TS 104-176 § 11.3.3.1).

**Spec:** GET is defined this way; POST /entityMaps (TS 104-176 § 11.2.3.2) exists
as an alternative with a Query body. Both forms CREATE an EntityMap.

**Our call:** Implemented as specified — GET creates (201), POST creates
(201). No-op fallback or listing endpoint added.

**Fix wanted:** GET should be safe (RFC 9110 § 9.2.1) — "do not request
any state change on the target resource". A method that creates a
resource on every call and burdens the broker with garbage-collected
EntityMaps is not safe. Either:
  - Bind creation to POST only, drop the GET form; or
  - Add a listing endpoint at GET /entityMaps (read-only), and keep
    creation on POST.
Rationale: intermediaries (proxies, browsers, crawlers, prefetchers)
routinely retry / prefetch GET requests assuming safety. A creating
GET generates a new resource per retry — classic anti-pattern.

---

<a name="16"></a>
## 16. TS 104-175 § 9.5.2 / TS 104-175 § 10.5 — `jsonldContext` fallback asymmetry: subs vs. CSRs

**Hit:** When `jsonldContext` is not specified on a CSR's
`contextSourceInfo`, forwarded requests go out with the original
request's @context handling (no compaction, no context substitution).
Subscriptions get different treatment for the same absent-field case.

**Spec:**

- For **Subscriptions** (TS 104-175 § 10.5.2.4 on create, analogous language on
  update):
  > "The @context to be used for sending Notifications ... shall be the
  > one specified in the jsonldContext field. If not present, the
  > jsonldContext field **shall be initialized** with the @context
  > applicable for the Subscription."

  Net: absent `jsonldContext` → broker auto-fills from the
  subscription-creation request's @context.

- For **CSRs** (TS 104-175 § 9.5.2 + TS 104-176 § 6.5.4): no analogous fallback clause.
  TS 104-175 § 9.5.2 only describes what happens when `jsonldContext` IS
  explicitly set (compact body + strip @context + Content-Type
  application/json). Nothing says the broker should auto-fill from the
  registration's @context.

**Our call:** Follow the spec as written. Subscriptions get the
fallback; CSRs do not — absent `jsonldContext` we forward with
per-element @context (application/ld+json array body) using the core
context.

**Fix wanted:** Make the fallback symmetric (CSRs also auto-initialise
`jsonldContext` from the registration-creation request's @context) or
state explicitly that the asymmetry is intentional. The subscription
and CSR mechanisms otherwise mirror each other closely; a diverging
default here is surprising.

---

<a name="15"></a>
## 15. TS 104-176 § 6.2.4 — @context placement for array bodies is ambiguous

**Hit:** Batch ops (TS 104-175 § 10.3.2 / 5.6.8 / 5.6.9 / 5.6.10 / 5.6.20) and ld+json
listings take/produce a JSON **array** as the payload body. JSON arrays
have no keys, so `@context` cannot appear "at the root of the body".

**Spec:** TS 104-176 § 6.2.4 says, for Content-Type `application/ld+json`, "the
associated @context shall be obtained from the request payload body
itself" and "No mixes are allowed" (referring to Link header vs. in-body
@context). The spec does not explicitly address arrays.

Two plausible readings:
- **Strict:** "body" means root — arrays cannot carry @context → ld+json
  would be disallowed for array bodies entirely.
- **Per-element:** array elements each carry their own @context; "no
  mixes" refers to Link ⊕ in-body @context, not to per-element within
  an array.

**Our call:** Per-element reading (what NGSI-LD listings already do in
practice — GET /entities with Accept: ld+json returns an array whose
elements each carry @context). An ld+json array body is valid iff EACH
element has its own `@context`. A missing `@context` on any element
raises BadRequestData. Link header with ld+json remains forbidden.

**Fix wanted:** TS 104-176 § 6.2.4 should add one sentence: "For array request
bodies with Content-Type `application/ld+json`, each element in the
array shall carry its own `@context`." Same clarification applies to
response bodies rendered as ld+json arrays.

---

<a name="17"></a>
## 17. TS 104-176 § 6.5.2 / TS 104-176 § 6.5.3 — `urn:ngsi-ld:request` substitution for receiverInfo

**Hit:** TS 104-175 § 9.5.1 mandates that a CSR's `contextSourceInfo` value of
`"urn:ngsi-ld:request"` be replaced by the same-named header from the
*triggering* request. TS 104-176 § 6.4.11 spells the HTTP-binding rule out. The
receiverInfo paragraphs (TS 104-176 § 6.5.2 entity notifications, TS 104-176 § 6.5.3 csource
notifications) describe an identically-shaped KeyValuePair[] but are
silent on `"urn:ngsi-ld:request"`. Reading strictly, the value must be
forwarded verbatim — defeating the same auth-forwarding use case the
contextSourceInfo wording was added for.

**Spec:** silent — TS 104-176 § 6.5.2/TS 104-176 § 6.5.3 only say "set equal to the content of
the corresponding 'value' member of the KeyValuePair".

**Our call:** symmetrically apply the substitution to receiverInfo on
the event-driven and CSR-side notify paths. Periodic-notification path
(TS 104-175 § 10.5 / TS 104-175 § 5.2.6.7.3) has no triggering request so a placeholder is
silently dropped there.

**Fix wanted:** TS 104-176 § 6.5.2 / TS 104-176 § 6.5.3 should reuse the TS 104-176 § 6.4.11 sentence:
"unless the special value `urn:ngsi-ld:request` has been set, in which
case the value is to be taken from the triggering request, if present
there." The data-model section (TS 104-175 § 5.2.6.6.1 receiverInfo) should also
mention the binding-specific substitution like TS 104-175 § 9.5.1 does for
contextSourceInfo.

---

<a name="19"></a>
## 19. TS 104-175 § 8.2.3 / TS 104-175 § 8.2.2 + TS 104-176 § 6.3.2 — InvalidRequest vs. BadRequestData for invalid URL-param values

**Hit:** A `GET /csourceSubscriptions?limit=-5&page=2` (or any URL with a
syntactically OK but semantically invalid pagination/value query param)
must be rejected. Both `InvalidRequest` and `BadRequestData` are
defensible per the wording in TS 104-175 § 8; the ETSI test suite consistently
expects `BadRequestData` (e.g. 041_04_01/02/03, 039_05_01).

**Spec:** Two different sections give two different rules:

- TS 104-175 § 8.2.3 (BadRequestData): "The request includes input data which does
  not meet the requirements of the operation." — URL-param values are
  arguably "input data".
- TS 104-175 § 8.2.2 (InvalidRequest): "The request associated to the operation is
  syntactically invalid or includes wrong content."
- TS 104-176 § 6.3.2: "If an HTTP request for an operation contains parameters that
  are incompatible with the operation, or it contains values of the
  options parameter that are not supported by the operation, an HTTP
  error response of type InvalidRequest should be returned." — but the
  qualifier "of the *options* parameter" ties this specifically to the
  `options=` URL param, not to `limit=` / `offset=` / `q=` / etc.

So the spec narrows InvalidRequest to (a) malformed-request-line cases
and (b) values of `options=`. Everything else lands in the wide
"input data does not meet the requirements" bucket → BadRequestData.

**Our call:** Initially returned InvalidRequest for any unknown or
out-of-range URL param value (uniform handling, less branching).
Switching to BadRequestData per TS 104-175 § 8.2.3 + the test suite's expectations.

**Fix wanted:** TS 104-176 § 6.3.2 / TS 104-175 § 8.2.3 / TS 104-175 § 8.2.2 should make the split
explicit:

- BadRequestData: invalid value for any URL param defined by NGSI-LD
  (limit, offset, q, attrs, georel, …) — i.e. "data wrong".
- InvalidRequest: unrecognized URL param OR an `options=` value that
  the implementation doesn't support — i.e. "request doesn't make
  sense".

Right now TS 104-175 § 8.2.3 wording overlaps with TS 104-175 § 8.2.2 wording closely enough
that two implementations can both quote the spec and still disagree.

---

<a name="18"></a>
## 18. TS 104-175 § 16.2.4 — snapshot capture from CSRs in no-split deployments

**Hit:** TS 104-175 § 16.2.4 says snapshot queries are executed "following the
behaviour described in clause 5.7.2.4" (the distributed-query path).
TS 104-175 § 10.4.3.4 in turn covers two regimes:
1. *split entities*: each Entity may be sharded across multiple CSRs;
   the broker must forward queries unfiltered, merge per-Attribute (§
   4.5.5.3), and re-apply filters post-assembly (TS 104-175 § 7.4.2.4).
2. *no-split*: every Entity is fully held by one source — used by the
   `splitEntities=false` URL param (TS 104-175 § 5.2.6.5.1) and indirectly by some
   broker-wide configuration switches.

For *snapshot creation* in particular, the no-split regime is enormously
faster because:
- the query can be forwarded WITH filters to each CSR,
- responses are simple inserts into the snapshot's storage (no merge),
- no post-merge filter scan over the captured set is needed.

**Spec:** TS 104-175 § 16.2.4 doesn't acknowledge the split/no-split distinction
at all, just defers to TS 104-175 § 10.4.3.4. Nothing forbids a broker from honouring
`splitEntities=false` on snapshot creation, but it's also not made
explicit. TS 104-175 § 5.2.6.5.4 (Snapshot data type) doesn't list `splitEntities` as
a member of the Snapshot doc, and TS 104-176 § 6.4.9 (NGSILD-Snapshot binding)
defines no URL params for the create endpoint.

**Our call:** allow `?splitEntities=true|false` on `POST /snapshots`,
parsed into the same `swNgsild.splitEntitiesSet/Val` used by `GET
/entities`. Default is the broker's `--noSplitEntities` setting. In
no-split mode capture forwards filters to CSRs, dedupes by id when
streaming into the snap-tenant ("first writer wins"), and skips the
post-merge filter scan. Split-mode is a follow-up.

**Fix wanted:**
- TS 104-175 § 16.2 should say explicitly that snapshot creation MAY accept
  `splitEntities` per TS 104-175 § 5.2.6.5.1, with the same semantics as on `GET
  /entities`.
- TS 104-176 § 6.4.9 should list URL params accepted on `POST /snapshots`, or
  point to TS 104-175 § 10.4.3 / TS 104-175 § 5.2.6.5.1 for the allowed set (so brokers don't
  diverge on which params apply where).
- TS 104-175 § 16.2.4 step "execute the Queries … following clause 5.7.2.4"
  should make the split / no-split branches of 5.7.2.4 explicit, since
  the implementations are very different on capture cost (TB-scale
  snapshots are practical only in no-split mode without Phase-2c
  per-attribute-merge work).

---

<a name="20"></a>
## 20. TS 104-175 § 10.5.7 — attributeDeleted notification representation: bare-string default vs. ETSI fixtures

**Hit:** ETSI test cluster 046_22_* (attributeDeleted notifications) ships
13 expectation fixtures under `data/subscriptions/expectations/` that
unconditionally expect the **object form**:

```json
"locatedAt": { "type": "Relationship", "object": "urn:ngsi-ld:null" }
```

The tested subscriptions set neither `sysAttrs`, `showChanges`, nor a
multi-instance `datasetId`.

**Spec:** TS 104-175 § 10.5.7, third bullet under "If a Subscription does not define a
timeInterval...":

> "If an Attribute has been deleted, **only the name of the attribute as
> key and the URI 'urn:ngsi-ld:null' as value shall be provided**, unless
> more information is required. The latter is the case, if:
>
> - a datasetId needs to be provided;
> - the notification.sysAttrs is set to true and thus the system generated
>   sub-attributes [...] have to be provided;
> - notification.showChanges is set to true and thus a previous value or
>   object has to be provided."

So the spec mandates the **bare string** as the default; the object form
is only required when one of those three triggers fires.

**Our call:** We ship the object form unconditionally (matches every
ETSI fixture). The bare-string default would be strictly conformant but
fails ~50% of 046_22_*. A code comment in
`ldSubscriptionNotify.c::buildNotifDataEntry` and a feedback memory record
the deviation so we can flip back when the fixtures are aligned.

**Fix wanted:** Either

- ETSI updates the 13 fixtures to use the bare-string form (then v1.9.1
  brokers pass without a deviation), **or**
- ETSI clarifies that TS 104-175 § 10.5.7 always requires the object form regardless
  of sysAttrs/showChanges/datasetId (then the spec text needs amending —
  the "shall" + "unless more information is required" wording today
  unambiguously mandates bare string as the minimum).

The `since_v1.6.1` tag on the affected tests suggests these fixtures may
predate a TS 104-175 § 10.5.7 wording change; the framework's
`SystemGeneratedTemporalPropertyOperator` already includes a `deletedAt`
hook, hinting at half-finished alignment.

---

<a name="21"></a>
## 21. TS 104-175 § 5.2.6.5.2 — entityDeleted/attributeDeleted trigger symmetry on entity delete

**Hit:** ETSI tests 046_22_12 / 046_22_13 (entity-delete cluster) configure
subscriptions with `notificationTrigger: ["attributeDeleted"]` only — no
`entityDeleted`. The fixture expects the subscription to fire when the
entity is deleted, with body shape:

```json
{
  "id": "...",
  "type": "Building",
  "deletedAt": "<iso>",
  "name": { "type": "Property", "value": "urn:ngsi-ld:null" }
}
```

(046_22_13 adds `sysAttrs` → entity-level + per-attribute
`createdAt`/`modifiedAt` from the pre-delete snapshot.)

**Spec:** TS 104-175 § 5.2.6.5.2 enumerates six triggers — `entityCreated`,
`entityUpdated`, `entityDeleted`, `attributeCreated`, `attributeUpdated`,
`attributeDeleted`. **Only** `entityUpdated` is explicitly stated to
subsume sub-events:

> "entityUpdated" is equivalent to the combination "attributeCreated",
> "attributeUpdated" and "attributeDeleted".

There is **no equivalent statement** for `entityDeleted` ⇒
`attributeDeleted` (or `entityCreated` ⇒ `attributeCreated`, though we
already do that side too).

TS 104-175 § 10.5.7 then says, for the body, "If the notification was triggered by
the deletion of an Entity and the notification.showChanges member is not
set to true, **only the deletedAt system property shall be provided**."
The fixtures contradict this — they include the watched attribute as a
null-marker entry alongside `deletedAt`.

**Our call:** Two deviations layered on each other to match the test:

1. `triggerMatches` — `LdNotifyEntityDelete` also fires for subs with
   `attributeDeleted` trigger (symmetric to the existing
   `LdNotifyEntityCreate` ⇒ `attributeCreated` mapping). Comment in
   `ldSubscriptionNotify.c` flags the spec gap.
2. `buildNotifDataEntry` — for `LdNotifyEntityDelete` events, append each
   non-keyword attribute the pre-delete entity had (subject to the sub's
   `watchedAttributes` filter) as a null-marker wrapper
   `{ type, value/object/languageMap: "urn:ngsi-ld:null" }`. With
   `sysAttrs`, also forward each attribute's `createdAt`/`modifiedAt` and
   inject a fresh `deletedAt` per attribute.

**Fix wanted:**

- Either add the `entityDeleted` ⇒ `attributeDeleted` symmetry statement
  to TS 104-175 § 5.2.6.5.2, **and** update TS 104-175 § 10.5.7's "only the deletedAt" rule to
  describe the watched-attribute body case explicitly, **or**
- Update the fixtures to use `notificationTrigger: ["entityDeleted"]` and
  drop the per-attribute body content (just `{id, type, deletedAt}`).

Either resolution would let us flip the deviation back to spec-strict
behaviour. Until then, our broker matches what `046_22_12/13` demand.

---

<a name="22"></a>
## 22. TS 104-175 § 10.2.7 — `?deleteAll=` boolean parsing case-sensitivity

**Hit:** Python's `requests` library serialises a Python `True` to the
literal string `"True"` (capital T) on URL query params. ETSI test
046_22_08 sends `?deleteAll=True`. The broker was strict-`strcmp("true")`
and silently treated it as false (deleted only `@none` instead of all
instances), causing the wrong notification.

**Spec:** TS 104-176 § 6.3.2 / TS 104-175 § 10.2.7 don't pin down the case for boolean URL
parameters. The "Boolean" datatype reference (TS 104-175 § 5.2.6.8.3) likewise leaves
case unspecified for query-string serialisation.

**Our call:** Case-insensitive parse (`strcasecmp`). Same change should
probably apply to other bool URL params (`details`, etc.) — currently
done only for `deleteAll`.

**Fix wanted:** Spec clarification on whether bool URL params must be
strict lower-case `true`/`false`, or whether implementations should
accept any case. If strict, the test framework should fix
`requests.delete(..., params={"deleteAll": "true"})` to send the
lower-case literal instead of letting Python serialise `True`.

---

<a name="23"></a>
## 23. TS 104-176 § 6.4.7 — 206 Partial Content vs 200 OK on temporal queries: ETSI tests are mutually inconsistent

**Hit:** ETSI temporal tests have contradictory expectations for the
HTTP status code on `GET /temporal/entities` and
`GET /temporal/entities/{id}`:

- *Most* of the suite — `020_01..12`, `020_16`, `020_21`,
  `021_01..14`, `021_17..19`, `021_21..23` (≈ 60 tests) —
  expects **200 OK** for any successful temporal retrieve, *even when
  the result has many instances and even when `lastN` was clipping*.
- A subset — `020_13_01..09`, `021_15_04..07`, `021_16_01` (≈ 11
  tests) — expects **206 + Content-Range** in shapes where no
  truncation should have occurred (e.g. `020_13_01` retrieves an
  entity with 20 instances per attribute, no `lastN`, no `timerel`,
  default broker cap = 100, so the broker has all the data the user
  could possibly want — yet the test asserts 206).
- A third subset — `020_05_01/02`, `021_03_01` — sets `lastN`
  smaller than the actual instance count and *also* expects **200**
  (i.e. expects no Content-Range even though clipping by `lastN`
  did occur).

**Spec:** TS 104-176 § 6.4.7 says "implementations *shall* use the Partial
Content Response (206) ... if the implementation is not able to respond
with the full representation at once". The natural reading is that 206
is conditional on the broker actually clipping the result. Under that
reading **none** of the three subsets is internally consistent with
the others; there is no single broker policy that satisfies the whole
suite simultaneously.

**Our call:** Stick with the strict-spec interpretation:
**206 only when the implementation actually had to clip** (default-cap
overflow, or `lastN` smaller than the number of available instances);
**200** otherwise. This satisfies the largest cluster (~60 tests)
but loses the 11 in 020_13/021_15/021_16 that demand "always 206 even
on full responses", plus the 3 in 020_05/021_03 that demand "200 even
when lastN clipped".

We tried the alternative ("always 206 when there's any temporal data")
during this session — it won 11 tests and flipped 62 PASS→FAIL, so
we reverted.

**Fix wanted:** ETSI tests need to be reconciled internally. The most
defensible policy is the strict spec reading; the 020_13/021_15/021_16
fixtures should be relaxed to accept either 200 or 206, and the
020_05/021_03 fixtures should be updated to expect 206 when clipping
occurs.

<a name="26"></a>
## 26. TS 104-175 § 12.3.3.5 — RegistrationInfo filtering on GET /csourceRegistrations is "should", not "shall"; client should be able to pick

**Hit:** `GET /csourceRegistrations?...` selects a CSR by matching the
query against any of its `information[]` entries, but the response
shape — full vs. filtered — is left to the implementation:

> "implementations **should** return filtered Context Source
>  Registrations, which only contain context source registration
>  information relevant for the query, in particular only matching
>  RegistrationInfo elements."

Two valid behaviours:
- **Filtered** (current swBroker default): strip non-matching
  RegistrationInfo entries — cleaner, less network noise.
- **Unfiltered**: return the CSR as registered — clients see context
  they did not query for but get the complete picture.

Today the choice is a build-time / configuration setting. The client
cannot ask for one shape or the other from a single broker.

**Our call:** broker defaults to filtered. A new CLI flag
`--testConformance` flips it to unfiltered, used by ETSI runs whose
fixtures expect that shape.

**Fix wanted:** add a URL param to TS 104-175 § 12.3.3.4 (e.g.
`?information=relevant|full`, default `relevant`) so the client picks
the shape per request. That removes the ambiguity ("should") and
keeps both behaviours accessible from one broker without server
restart or vendor-specific switches.

---

<a name="27"></a>
## 27. TS 104-175 § 9.1 — concurrent vs sequential forwarding to matching CSRs

**Hit:** When a service routine matches N CSRs (across exclusive,
redirect, inclusive, auxiliary), the natural implementation is a
sequential loop `for csr in csrs: forward(csr)`. With N=5 and a
non-responsive CSR endpoint at the 5-second default, that's 25 s of
wall-clock for a request that should be ~5 s.

The fix is to fan out concurrently (curl_multi_perform or equivalent),
but the spec gives no guidance — neither permission nor obligation.

**Spec:** TS 104-175 § 9.1 talks about avoiding overquery but says nothing
about ordering or concurrency of the forwards. TS 104-175 § 10.4.2.4 / TS 104-175 § 10.4.3.4
describe the processing flow as if it were sequential ("first
exclusive, then redirect, then inclusive"), but those orderings are
about *result composition*, not *request issuance*.

**Our call:** issue all matched forwards concurrently in a single
batch; walk the responses in the sequential order the spec describes
for result composition. Semantically identical to serial dispatch.

**Fix wanted:** an explicit "the Context Broker MAY issue forwarded
requests to matching Context Sources concurrently; the result
composition order defined in TS 104-175 § 10.4.2.4 / TS 104-175 § 10.4.3.4 / TS 104-175 § 9.3.3 is
unaffected." Otherwise an interop reviewer can argue "the spec
implies sequential."

---

<a name="28"></a>
## 28. TS 104-175 § 5.2.6.6.6 — `timeoutMs` semantics under concurrent multi-CSR fan-out

**Hit:** Each CSR may set its own `timeoutMs`. When the broker fans
out to N CSRs concurrently, the natural single-deadline backend
(curl multi, epoll) wants one overall budget, not N independent
deadlines. The spec defines a per-CSR value but is silent on how
that composes when multiple CSRs are active simultaneously.

**Spec:** TS 104-175 § 5.2.6.6.6 defines `timeoutMs` on `CSourceRegistration` as
"the maximum time the Context Broker will wait for a response from
the Context Source". Says nothing about multi-CSR semantics.

**Our call:** overall batch deadline = `max(per-CSR timeoutMs over
all matched CSRs)`, then per-CSR cancel when its own timeoutMs is
reached. A fast CSR is not penalised by a slow peer.

**Fix wanted:** spec should state explicitly: "Each forwarded
request has its own timeoutMs; concurrent forwards do not share a
deadline. A request shall be cancelled when its own timeoutMs
expires, irrespective of peer requests still in flight."

---

<a name="29"></a>
## 29. TS 104-175 § 9.5.1 — outbound header policy on forwarded requests

**Hit:** The broker forwards a request to a CSR. Which headers
*must* it add, which *may* it add, which *must* it copy verbatim
from the inbound request? Today's spec text addresses only a subset
(contextSourceInfo, banned keys, jsonldContext → Link). The
mandatory baseline is not enumerated.

**Spec:** TS 104-175 § 9.5.1 enumerates banned `contextSourceInfo` keys
(Content-Length, Host, NGSILD-Tenant, ngsildConformance). TS 104-175 § 10.6.3
defines `Via` for loop detection. TS 104-175 § 5.2.6.4.5 / annexes touch
NGSILD-Tenant on the wire. No single section says "every forwarded
request shall carry headers X, Y, Z" or "the inbound headers A, B, C
shall be propagated".

**Our call:**

| header           | policy                                                  |
|------------------|---------------------------------------------------------|
| Via              | append own-alias; preserve inbound Via chain            |
| NGSILD-Tenant    | set to csr.tenant (TS 104-175 § 5.2.6.5.3 rewrite)                     |
| Content-Type     | default `application/ld+json`; csi.contentType override |
| Accept           | set from csi.accept (no default)                        |
| Link             | from csi.jsonldContext                                  |
| Authorization    | dropped (not propagated)                                |
| X-Forwarded-For  | not added (we don't track this today)                   |
| inbound user hdrs| not propagated unless named in csi                      |

**Fix wanted:** TS 104-175 § 9.5.1 should have a table identical to the
above (or its agreed equivalent). Without it, two compliant brokers
can disagree on outbound shape, with downstream CSRs seeing
different headers depending on broker vendor.

---

<a name="30"></a>
## 30. TS 104-175 § 10.4.3.4 — split-mode forwarded query: which URL params survive?

**Hit:** In "split entities" mode, a single entity's attributes may
live on multiple CSRs. Forwarding the user's `q`, `geoQ`, or `scopeQ`
to one CSR is unsafe because that CSR sees only part of the entity
and may falsely exclude it. The broker must therefore strip those
filters from the forwarded URL — but the spec doesn't enumerate
*which* filters are unsafe.

**Spec:** TS 104-175 § 10.4.3.4 mentions split-entities behaviour in passing
("if the broker holds part of an Entity locally"), without giving
the list of forwardable params.

**Our call:** in split mode, forward only `id`, `idPattern`, `type`
(guaranteed-on-every-fragment per TS 104-175 § 8.5 / TS 104-175 § 5.2.6.4.4). Strip
`q`, `geoQ`, `scopeQ`, `attrs`, `format`, `local`, `entityMap`,
`orderBy`, `collation`. `pick` MAY be narrowed per CSR (intersect
with the CSR's registered exports).

**Fix wanted:** TS 104-175 § 10.4.3.4 should contain an explicit table:
"In split-mode federation, the following URL params SHALL be
stripped from the forwarded request: …; the following params MAY be
narrowed per CSR: …; the following are forwarded as-is: …".
Without that table, interop on the same query across vendors is a
coin toss.

---

<a name="31"></a>
## 31. TS 104-175 § 14.5.4 — multi-CSR EntityMap aggregation

**Hit:** GET /entityMaps under federation: each matched CSR returns
its own EntityMap. The broker must build a single EntityMap response
that captures *which entity came from which CSR(s)*. The spec
defines the single-source EntityMap shape but not the aggregation.

**Spec:** TS 104-175 § 14.5.4 defines `entityMap` as `{ <entityId>: [src1,
src2, …] }`. TS 104-175 § 5.2.6.8.3 mentions `linkedMaps` for cross-broker
references. Nothing ties them together for the aggregation flow.

**Our call:** broker's response includes:
- `entityMap`: union over all CSRs' entries, deduped by entityId,
  source array merged.
- `linkedMaps`: `{ <csr.regId>: <remote-map-id> }` so the client
  can re-page the remote map directly. The local map's source
  array for an entity is the set of `regId`s that returned it.

**Fix wanted:** TS 104-175 § 14.5.4 should formalise the aggregation:
linkedMaps shape, dedup rule, and the source-array semantics when
the same entity appears on multiple CSRs (we use union; some might
argue "first wins").

---

<a name="32"></a>
## 32. TS 104-175 § 5.2.6.4.4 — ProblemDetails extension fields for NGSI-LD errors

**Hit:** Every error response in our broker that names a specific
entity wants to surface that entity's id. BatchEntityError already
carries it; plain ProblemDetails (the 400/404/409 family) doesn't.
Today the entityId is buried in the `detail` string, which is
human-prose only.

**Spec:** TS 104-175 § 5.2.6.8.5 defines BatchOperationResult / BatchEntityError
with `entityId`. TS 104-175 § 5.2.6.4.4 defines ProblemDetails per RFC 7807 with
`type`, `title`, `detail` and "MAY contain additional members".
The "MAY" is technically permission, but no recommended extension
exists — every vendor invents their own field names.

**Our call (proposed, not yet shipped):** standardise an extension
namespace and add:
- `entityId` (string) — when the error pertains to a single entity
- `entityIds` (string[]) — when it pertains to several
- `attributeName` (string) — for attribute-scoped errors
- `registrationId` (string) — for distop forwards that failed
- `status` (int) — echo HTTP status in the body (clients that drop
  the response line still see it)

**Fix wanted:** TS 104-175 § 5.2.6.4.4 / Annex B should bless a small set of
NGSI-LD-specific ProblemDetails extension keys. The current
"undefined" position guarantees vendor lock-in.

---

<a name="33"></a>
## 33. TS 104-175 § 10.2.10 — Replace Entity: can the type change?

**Hit:** PUT /entities/{id} with `type: B` against a stored entity
of `type: A`. The spec wants "complete replacement". Allowing the
type to change has cascading implications: subscriptions filtered
by type, TRoE indices, type-scoped CSR matches, etc.

**Spec:** TS 104-175 § 10.2.10.4 — "If the target Entity exists locally,
completely replace the existing Entity with the same Entity ID with
the new Entity content provided." "Completely replace" is permissive
of type change; nothing forbids it.

**Our call:** refuse with 400 BadRequestData when the body type
differs from the stored type. Replace = same-id, same-type swap of
attribute content.

**Fix wanted:** spec should pick one:
(a) Replace SHALL preserve the stored type; type change requires a
delete-then-create sequence, or
(b) Replace MAY change the type, and the spec enumerates the
ripple effects on subs / TRoE / matchers / CSR coverage.

Picking (a) is much smaller; today (b) is implicit but
uncharacterised. Either is fine; the silence is not.

---

<a name="34"></a>
## 34. TS 104-175 § 10.2.9 + TS 104-175 § 8.5.3 — Merge Entity composition order across local + multiple CSRs

**Hit:** PATCH /entities/{id} (Merge) against an entity with attrs
distributed across local + N inclusive CSRs. Each side may apply
the merge differently (different `defaultObservedAt`, instance-level
conflict resolution per TS 104-175 § 8.5.3). The single response body wants
to be the merged-entity view, but it's a composition of N+1
results.

**Spec:** TS 104-175 § 10.2.9 describes single-source merge. TS 104-175 § 8.5.3
describes per-instance conflict rules within one source. Nothing
defines the order or rule when several sources merge in parallel
and the broker must compose them.

**Our call:** local merge first (the broker's own view), then walk
forwards in mode order (exclusive → redirect → inclusive). Each
remote source's result is re-merged into the running composite via
TS 104-175 § 8.5.3 timestamp rules. This is deterministic but vendor-policy.

**Fix wanted:** TS 104-175 § 10.2.9 should state the composition order
explicitly, OR explicitly say "the composition order is
implementation-defined; the resulting entity SHALL satisfy
TS 104-175 § 8.5.3 against the union of all per-source results". The latter
is weaker but at least removes the "is order observable?" question.

---

<a name="35"></a>
## 35. TS 104-175 § 10.4.2.4 — Auxiliary mode result merge under concurrent fan-out

**Hit:** Auxiliary mode "fills gaps only" — its attributes are added
to the composite only where no earlier source provided them. With N
auxiliary CSRs in flight concurrently, two of them may both contribute
attr X. Which one wins?

**Spec:** TS 104-175 § 9.3.2 / TS 104-175 § 10.4.2.4 describe auxiliary as gap-filler
without ordering. "Concurrent two-aux overlap" is unspecified.

**Our call:** walk auxiliary results in CSR-cache-iteration order
(stable but vendor-internal); first-write wins for any attr the
composite is still missing.

**Fix wanted:** TS 104-175 § 10.4.2.4 should pick:
(a) the composite is order-independent — two aux CSRs claiming the
same attr is "any of them wins, undefined which", and clients shall
not rely on a specific source, or
(b) the broker SHALL apply TS 104-175 § 8.5.3 timestamps across aux sources
too (most-recent wins), or
(c) auxiliary CSRs SHALL not overlap — overlap is a configuration
error the broker MAY reject at CSR creation.

Today every broker silently picks one of these.

---

<a name="36"></a>
## 36. TS 104-175 § 5.2.6.4.8 — distop counter atomicity under concurrent fan-out and HA

**Hit:** `timesSent`, `timesFailed`, `lastSuccess`, `lastFailure`
are per-CSR counters intended to expose distop health. With
concurrent forwards from a single broker they update from one
thread (epoll callback) — fine. But:

- Two broker replicas behind a load balancer sharing one mongo CSR
  store: each replica increments independently; without `$inc` the
  reads race. We use `$inc` exactly to avoid this.
- A single broker fanning out concurrently to the same CSR twice
  (e.g. distributed sub fan-out on top of distop fan-out): the
  counters should reflect both attempts.

The spec defines what the counters mean but not their update
semantics under concurrency.

**Spec:** TS 104-175 § 5.2.6.4.8 lists the counters as part of CSR state. No
section addresses concurrent update or HA.

**Our call:** atomic `$inc` on every increment, `$max`/`$min` for
lastSuccess/lastFailure timestamps. Updates are best-effort flushed
periodically, not synchronously per forward.

**Fix wanted:** TS 104-175 § 5.2.6.4.8 should say "Context Brokers SHALL ensure
counter updates are atomic with respect to concurrent forwards
issued by the same broker or by replicas sharing the CSR store.
Updates MAY be deferred (eventual consistency) but SHALL be
monotonic." Otherwise interop is a vendor lottery.

---

<a name="37"></a>
## 37. TS 104-175 § 10.2.4.4 — `?options=noOverwrite` semantics when the same attr is on a CSR

**Hit:** POST /entities/{id}/attrs with `?options=noOverwrite=true`.
The local entity already has attr X. An exclusive CSR also claims X.

- Local rule (TS 104-175 § 10.2.4.4): noOverwrite means "skip attrs already
  present".
- Distop rule (TS 104-175 § 9.3.3): exclusive CSR owns its claimed attrs,
  local SHALL NOT hold them.

The two collide:
- If the broker forwards X to the CSR, the CSR may itself apply
  noOverwrite based on *its own* state and reject — or not.
- If the broker also chops X from local input, the local
  noOverwrite-skip never triggers because X is gone.
- The response body should report X under `notUpdated` (noOverwrite
  skip) or `updated` (CSR took it) — depending on what the CSR did,
  not the local state.

**Spec:** TS 104-175 § 10.2.4.4 describes the local case; TS 104-175 § 9.3.3 describes
the exclusive case. The interaction is silent.

**Our call:** chop precedes local noOverwrite. The forwarded
request includes `?options=noOverwrite`. Whatever the CSR reports
back drives the per-attribute outcome in `updated/notUpdated`.

**Fix wanted:** TS 104-175 § 10.2.4.4 should add: "When `options=noOverwrite`
combines with forwarded operations, the URL param SHALL be
preserved on the forward; the CSR's per-attribute outcome
determines the response classification for that attribute,
overriding any inference the broker might make from local state."

---

<a name="38"></a>
## 38. TS 104-175 § 10.2.7 — `?deleteAll=true` forwarded to a CSR that doesn't support `deleteAttrs`

**Hit:** DELETE /entities/{id}/attrs/{attr}?deleteAll=true against
an entity that lives partly on an exclusive CSR. The CSR's
operations list doesn't include `deleteAttrs`. What happens?

Options:
- (a) Refuse the entire request (409 Conflict) — preserves
  consistency but loses the local copy unnecessarily.
- (b) Forward anyway (CSR will return Conflict), then delete locally
  — inconsistent but proceeds.
- (c) Skip the CSR (record per-attribute Conflict in errors[]),
  delete locally — the chop stands.

**Spec:** TS 104-175 § 10.2.7 describes the local delete. TS 104-175 § 9.3.3 mandates
the exclusive chop. TS 104-175 § 10.2.2.4 has analogous language for
createEntity ("Conflict ... or partial success"), but Delete is
silent.

**Our call:** option (c). Record `{ entityId, error: Conflict,
detail: "exclusive registration does not support deleteAttrs",
registrationId }` in errors[], local delete proceeds. Response is
207 if local-success + per-attr conflict, 409 if no local success.

**Fix wanted:** TS 104-175 § 10.2.7 should mirror the explicit clause already
in TS 104-175 § 10.2.2.4 — naming the partial-success / Conflict outcome
explicitly so vendors don't pick (a), (b), or (c) on a whim.

---

<a name="39"></a>
## 39. TS 104-175 § 10.2 (generic) — collapsing uniform-error multi-CSR responses

**Hit:** A distributed write operation matches N CSRs; every single
one returns the same error type (e.g. ResourceNotFound on a non-
existent entity, or Conflict). The natural shape is 207 Multi-Status
with N entries — but a 207 carrying N identical "Not Found" entries
is noise. A single 404 + ProblemDetails is what the client expects.

**Spec:** TS 104-175 § 5.2.6.8.5 defines BatchOperationResult (207). No section
addresses collapsing.

**Our call:** when every `errors[]` entry shares the same
`error.type`, collapse:
- All ResourceNotFound → 404 + plain ProblemDetails.
- All Conflict → 409 + plain ProblemDetails.
- Otherwise → 207 Multi-Status.

The collapsed body includes `entityId` (single entity) or
`entityIds` (multiple) as ProblemDetails extensions (see entry 32).

**Fix wanted:** TS 104-175 § 5.2.6.8.5 should bless the collapse rule explicitly,
naming the two ergonomic cases (ResourceNotFound → 404, Conflict →
409) and reserving 207 for genuinely mixed outcomes. Vendors that
return 207 for every multi-CSR response (the strict reading) bury
the actual cause behind a Multi-Status envelope.

---

<a name="40"></a>
## 40. TS 104-175 § 7.4.2.2 — chronological order on batch arrays is an unenforceable assumption

**Hit:** Spec assumes the client orders array elements (in
postEntityBatch* bodies, in temporal POST instance arrays, etc.) by
time-of-arrival. The broker uses array index as time. But nothing in
the body lets the client *assert* an order — and the broker can't
verify one, because the timestamps (createdAt/modifiedAt) are
server-stamped on arrival, not carried by the client.

So the "assumption" is functionally a hope. Two valid clients can
submit the same logical sequence in different array orders and get
divergent observable state.

**Spec:** TS 104-175 § 7.4.2.2 — "The Context Broker may assume that array
elements are provided in chronological order of arrival." No
mechanism, no error on out-of-order, no signal of intent.

**Our call:** trust the array order; document the convention in
`project_batch_multi_instance_ordering` memory.

**Fix wanted:** either
(a) drop the "may assume" sentence — order is not specified, ties
are broken by some deterministic rule (e.g. lexicographic on
attribute identity), or
(b) add an explicit assertion mechanism: `NGSILD-RequestSequence`
header, or a `sequence` field on batch items, or a request param
`?orderedByClient=true` that toggles strict order-as-time
interpretation.

Today every broker silently picks (a) or (b) with no observable
signal to the client.

---

<a name="41"></a>
## 41. TS 104-175 § 8.5 — multi-instance with identical datasetId on intake

**Hit:** POST /entities or POST /entities/{id}/attrs with an
attribute whose value is an array of N instances — two of which
have the *same* `datasetId`. What should happen?

Options:
- Reject with 400 BadRequestData (the input is internally
  contradictory).
- Silently dedupe — keep the last one, or first one, or run
  TS 104-175 § 8.5.3 timestamp resolution between them.
- Accept both, but the storage layer subsequently picks one (which?).

**Spec:** TS 104-175 § 8.5 defines `datasetId` as the unique key within an
attribute. TS 104-175 § 8.5.3 describes inter-source merge resolution but
*assumes* each source has already deduped its own instances. The
intake case (one client, two instances with same dsKey in one
request) is silent.

**Our call:** dedupe at intake (`ldDatasetIdDedup`) per TS 104-175 § 8.5.3
timestamp rules: most-recent wins. The client sees only one
instance survive.

**Fix wanted:** TS 104-175 § 8.5 should say explicitly: "If the input
payload contains multiple instances of the same Attribute with
identical datasetId, the Context Broker SHALL reject with
BadRequestData / SHALL dedupe per TS 104-175 § 8.5.3 / SHALL accept all and
defer resolution to ...". Three vendors will pick three answers
otherwise.

---

<a name="42"></a>
## 42. TS 104-175 § 7.7 — `joinLevel` semantics with circular references

**Hit:** GET /entities/{id}?join=inline&joinLevel=3 against an
entity graph where A → B → C → A (cycle). `containedBy` is named in
the spec for cycle prevention, but the per-step semantics are
unclear:
- Does the broker stop at depth 3 unconditionally, or at the cycle
  closing back to A regardless of depth?
- If A is the originally-requested entity, does it appear "inlined
  inside itself"?
- When two distinct paths to the same target exist
  (diamond: A → B → D, A → C → D), is D expanded twice or once?

**Spec:** TS 104-175 § 7.7 + TS 104-175 § 10.4.2.4 introduce `joinLevel`, `join`, and
mention `containedBy` cycle prevention. The interaction matrix
(depth × cycle × diamond × originating-id) isn't laid out.

**Our call:**
- Cycle detection by id only — if a target id is already in the
  containedBy chain for the current path, skip the expansion.
- Diamond: each path expands independently; no global dedup of
  expansions.
- joinLevel applies post-cycle: a cycle-closure counts as a "stop",
  not as a step.

**Fix wanted:** TS 104-175 § 7.7 should pin down the four-way interaction
in a small worked example (the same example we cite here) so two
brokers given the same graph + same `join=inline&joinLevel=3`
produce the same response.

---

<a name="43"></a>
## 43. TS 104-175 § 5.2.6.4.10 — JsonProperty inner-value @context interaction

**Hit:** A JsonProperty's `json` field is "opaque" — the spec says
its contents are NOT subject to JSON-LD expansion. But what counts
as "the contents"?

- If the JSON contains a key like `"speed"` that happens to match
  an NGSI-LD attribute name on the same entity, is that key
  expanded to its full IRI on output? (We say no — opaque means
  opaque.)
- If the JSON contains a `@context` key, is that consumed by JSON-LD
  processing, or rendered verbatim?
- Does the JsonProperty's *outer* wrapper (`type: JsonProperty`,
  `json: ...`) get expanded, while the inner `json` is preserved
  byte-for-byte?

**Spec:** TS 104-175 § 5.2.6.4.10 says JsonProperty's value is opaque. The
expansion boundary is intuitive but not normative.

**Our call:** strict boundary — the `json` value is treated as a
black box. Inner `@context` is data, not directive. The outer
wrapper is normal NGSI-LD.

**Fix wanted:** TS 104-175 § 5.2.6.4.10 should add: "The Context Broker SHALL NOT
process JSON-LD constructs inside the `json` value, including but
not limited to `@context`, `@id`, `@type`, `@vocab`, prefixed
names. The value SHALL be preserved byte-for-byte (up to
JSON-canonicalisation) between intake and retrieval."

---

<a name="44"></a>
## 44. TS 104-175 § 10.2.4.4 — `?observedAt=<time>` default-observedAt URL param

**Hit:** Append Attributes / Update Attributes / Merge Entity all
benefit from a default `observedAt` value that the broker injects
into each attribute instance that doesn't already carry one. Today
we accept `?observedAt=` for Merge (TS 104-176 § 7.3.3.2) and apply it during
ldEntityMerge.

The URL param is mentioned in passing for Merge but absent from the
Append / Update routes — yet the same use case ("I just sampled
these N sensors at time T, attach T to all of them as observedAt")
applies equally to Append.

**Spec:** TS 104-176 § 7.3.3.2 (Merge) — `observedAt` URL param defined.
TS 104-176 § 7.3.2 (Update), TS 104-175 § 10.2.4.4 (Append) — no equivalent.

**Our call:** Merge only, matching the spec literally. Clients
wanting the same on Append/Update must rewrite the payload.

**Fix wanted:** TS 104-175 § 10.2.4.4 / TS 104-176 § 7.3.2 should adopt the same
`?observedAt=` semantics as TS 104-176 § 7.3.3.2. Same shape, same
default-injection rule.

---

<a name="45"></a>
## 45. TS 104-175 § 5.2.6.5.3 — CSR `location` shape: bare GeoJSON Geometry vs full GeoProperty?

**Hit:** A CSR's `location` (and `observationSpace`, `operationSpace`)
field — what's its shape?

- Per TS 104-175 § 5.2.6.5.3 + TS 104-175 § 5.2.6.4.5, it's a GeoJSON Geometry: `{ "type":
  "Polygon", "coordinates": [...] }`. Bare geometry, no NGSI-LD
  wrapper.
- Per the NGSI-LD entity rendering rules, a Property of type
  GeoProperty is `{ "type": "GeoProperty", "value": { GeoJSON } }`.
  Wrapper required.

The CSR's `location` is *not* an entity-level GeoProperty — it's a
cross-domain ontology field, simplified shape only. But ETSI
fixtures and several implementations have submitted CSRs with the
wrapper form, and brokers have had to accept both.

**Spec:** TS 104-175 § 5.2.6.4.5 → bare GeoJSON Geometry on CSR location. TS 104-175 § 5.2.6.5.3
Example C.3 confirms (bare polygon). But the normative text doesn't
contrast against the wrapped form, and at least one ETSI fixture
sends the wrapped form to a CSR.

**Our call:** strict bare-geometry on intake (spec-conformant);
clients sending the wrapper get 400. We previously tried "intake
normalize" (auto-wrap → unwrap) but reverted — see commit
`98efede`.

**Fix wanted:** TS 104-175 § 5.2.6.5.3 should explicitly say: "The `location`,
`observationSpace`, and `operationSpace` fields of a
ContextSourceRegistration are GeoJSON Geometry objects per TS 104-175 § 5.2.6.4.5.
They are NOT NGSI-LD GeoProperty representations; the wrapper
shape `{ "type": "GeoProperty", "value": ... }` SHALL NOT be
accepted." And the ETSI fixture that sends the wrapped form should
be fixed accordingly.

---

<a name="46"></a>
## 46. TS 104-176 § 6.3.5 — "redirect" mode classified as BOTH single-source and multi-source (contradiction)

**Hit:** The status-code mapping table in TS 104-176 § 6.3.5 lists two
separate cases:

> In the case of an **exclusive or redirect** registration, where all
> of the data is held outside of the Context Broker and held in a
> **single registered source**, the following errors shall be
> returned: 508 / 504 / 404 / 502.

> In the case of an **inclusive or redirect** registration, where
> an entity is distributed over **multiple equally valid endpoints**,
> but when updating the state of the distributed entity, an error
> response is returned from one or more registered sources: 207
> Multi-Status.

`redirect` appears in *both*. The two paragraphs are mutually
exclusive ("single registered source" vs "multiple equally valid
endpoints"). Pick one — `redirect` cannot be both classifications
simultaneously.

**Spec:** literally as quoted above, TS 104-176 § 6.3.5.

**Our call:** treat `redirect` as multi-source (the 207 path) —
matches TS 104-175 § 9.3.3 where redirect explicitly allows multiple co-
existing CSRs. We use 502 generically for transport failure of a
single match.

**Fix wanted:** TS 104-176 § 6.3.5 should clearly group:
- `exclusive` → at most one source → 508 / 504 / 404 / 502
- `redirect` → multiple co-existing sources → 207 (single failure)
  / 502 (all fail with mixed) / 504 (all timeout)
- `inclusive` → 207 on any failure
- `auxiliary` → 207 on any failure (or silent, since aux is gap-
  fill)

The current text reads as a copy-paste accident.

---

<a name="47"></a>
## 47. TS 104-176 § 6.3.5 — `NGSILD-Warning` header emission semantics

**Hit:** Spec defines four IANA Warning Codes — 110 / 111 / 199 /
299 — for distop responses. But when exactly to attach them is
underspecified:

- 110 "Response is Stale" — emit on EVERY response that included
  cached data, or only on the first cache hit? Both? On 200 OK
  responses where mixed (some cached, some live)?
- 111 "Revalidation Failed" — does this REPLACE the actual error,
  or accompany a 502/207?
- 199 "Miscellaneous Warning" — covers "no response in time" AND
  "loop detected". Same code, two distinct causes; client can't
  distinguish.
- 299 "Miscellaneous Persistent Warning" — same issue, generic for
  any 4xx the upstream returned.

**Spec:** TS 104-176 § 6.3.5 (Table "NGSI-LD Warning Codes").

**Our call:** we don't emit any NGSILD-Warning headers today. The
status code + ProblemDetails body carry the same information
sufficiently. ETSI fixtures haven't asserted on these warnings yet.

**Fix wanted:** TS 104-176 § 6.3.5 should define for each warning code:
(a) the exact triggering condition (single attribute? cached vs
live response? loop vs timeout?),
(b) the HTTP status code(s) it MAY accompany (200? 502? 207?),
(c) whether multiple warning codes can appear simultaneously on
the same response.

Without that, the warning header is an interop dead-letter — every
broker that wants to claim conformance just doesn't emit it.

---

<a name="48"></a>
## 48. TS 104-175 § 8.5.3 — "indeterminate / random" tiebreaker is unfit for interop

**Hit:** Conflict resolution for two Attribute instances sharing a
`datasetId`, both lacking `observedAt` and `modifiedAt` (or with
identical values):

> If no other mechanism for determining the most current Attribute
> instance is found, the NGSI-LD system shall choose the Attribute
> instance at random and the result is indeterminate.

"Indeterminate" is acceptable for a single broker; for federation
across brokers it is a guaranteed interop failure — same input,
different output depending on which broker handled the merge.

**Spec:** TS 104-175 § 8.5.3, verbatim.

**Our call:** deterministic last-wins by array index (which is the
client-supplied ordering per TS 104-175 § 7.4.2.2). Two of our brokers given
the same input produce the same output; two of someone else's
brokers may not.

**Fix wanted:** spec should mandate a deterministic tiebreaker:
- (a) array index (last in body wins), or
- (b) lexicographic on the Attribute's full serialization, or
- (c) a server-stamped `createdAt` ULID,

so "given same input bytes, two compliant brokers produce same
output bytes". "Indeterminate" should be reserved for non-
observable internal states, not response data.

---

<a name="49"></a>
## 49. TS 104-175 § 8.5.1 — `datasetId: "@none"` round-trip drops the field

**Hit:** Client sends `{"speed":{"type":"Property","value":42,
"datasetId":"@none"}}`. Per TS 104-175 § 8.5.1 this is equivalent to
omitting datasetId — it identifies the default instance. On
retrieval, the response renders the default instance *without* the
datasetId. So:

```
POST /entities  body: { ..., "speed": {..., "datasetId": "@none"} }
GET  /entities/{id}  → { ..., "speed": {...}  }   ← datasetId gone
```

The same POST body is no longer reproducible from the GET. Tools
that "diff" expected-vs-actual JSON, or that pipe one entity into
another broker, see a perceived difference.

**Spec:** TS 104-175 § 8.5.1 says "@none" is the default instance AND says
"the datasetId of the default Attribute instance is never
explicitly included in responses". Together: round-trip diverges.

**Our call:** match the spec strictly — strip datasetId on response
when it's "@none". Document the rule.

**Fix wanted:** spec should either:
(a) say explicitly "POST→GET round trips may drop `datasetId:
@none`; client-side diff tools should normalize", or
(b) preserve `datasetId: @none` on response when present in input
(no information loss); the renderer chooses, and the implicit-
default reading still works.

(b) is friendlier to round-trip tools.

---

<a name="50"></a>
## 50. TS 104-176 § 6.4.2 / TS 104-175 § 5.3.3.3 — GeoJSON representation negotiation: Accept header vs format param

**Hit:** A client wants entities rendered as GeoJSON Feature
objects (TS 104-175 § 5.3.3.3). Two ways to ask:

- `Accept: application/geo+json`
- `?format=geojson` (or similar URL param)

The spec doesn't pin which one is normative. ETSI fixtures use
Accept; some interop guides reference a URL param. The selection
of geometry property (`?geometryProperty=location` vs another) is
itself a separate axis.

**Spec:** TS 104-175 § 5.3.3.3 defines the GeoJSON representation. TS 104-176 § 6.4.4
mentions GeoJSON in passing. TS 104-176 § 6.4.2 covers representation
selection but doesn't enumerate GeoJSON specifically.

**Our call:** `Accept: application/geo+json` triggers GeoJSON
rendering. `?format=geojson` is also accepted as an alias.
`?geometryProperty=` selects the geometry source attribute (default
"location"). Same logic on collection (FeatureCollection) and
single-entity (Feature) endpoints.

**Fix wanted:** TS 104-176 § 6.4.2 should list the GeoJSON representation
explicitly, name `Accept: application/geo+json` as the canonical
trigger, name the geometryProperty interaction explicitly, and
state whether `?format=geojson` is a normative alias or a vendor
extension.

---

<a name="51"></a>
## 51. TS 104-175 § 7.4 + TS 104-176 § 6.4.7 — `limit` / `offset` semantics under distributed federation

**Hit:** GET /entities?limit=20&offset=40 over an entity space
distributed across 5 CSRs. Is `limit=20` per-CSR (each returns up
to 20, broker dedupes locally → may exceed 20 in the response) or
global (broker stops aggregating at 20 across all sources)? Where
does `offset=40` apply — globally, or first-N from broker view,
then the remaining offset against the federated set?

EntityMap (TS 104-175 § 14) addresses the *consistency* of pagination
(snapshot of IDs so subsequent pages return the same entities),
but not the FIRST-page semantics.

**Spec:** TS 104-175 § 7.4 defines limit/offset on a single broker. TS 104-176 § 6.4.7
covers the HTTP binding. TS 104-175 § 14 covers EntityMap. The composition
of limit/offset across federated sources is not addressed.

**Our call:** first page — request each CSR with the user's
limit/offset verbatim, merge + dedup locally, trim to user's
limit. EntityMap captures the resulting ID set for subsequent
pages.

**Fix wanted:** TS 104-175 § 7.4 should add a sub-clause: "Under
distributed federation, limit and offset apply to the broker's
merged result set. The Context Broker MAY forward limit and offset
to each source as a hint, but the merged-set bound is normative.
EntityMap (TS 104-175 § 14) provides consistent pagination across follow-up
page requests."

---

<a name="52"></a>
## 52. TS 104-175 § 10.5.7 — distributed-subscription notification: "except for the one from which the notification has been received"

**Hit:** Federated subscription path. A broker receives a notification
from a remote source A; per TS 104-175 § 10.5.7 it should retrieve the same
entities from all OTHER known sources (locally + every CSR EXCEPT A)
and merge. How does the broker know which source the notification
came from? Subscription.subscriptionId? Via header? Source URI?
The exclusion criterion is not specified.

If the broker can't reliably identify A, it either (a) double-counts
(asks A again, gets the same data, merges with itself) or (b) skips
nothing (re-fans-out to all sources, defeating efficiency).

**Spec:** TS 104-175 § 10.5.7 — "all Context Sources that have information
about these Entities, except for the one from which the
Notification has been received." No mechanism described.

**Our call:** identify A by inspecting the inbound notification's
`Via` chain plus the corresponding csr-subscription. The CSR that
originated the chain is the most recent Via entry; we skip it
during the local fan-out.

**Fix wanted:** TS 104-175 § 10.5.7 should pin the mechanism. Two reasonable
choices:
(a) the notification SHALL include a header (or top-level field)
naming the originating CSR's regId / contextSourceAlias, or
(b) the broker SHALL trust the last `Via` entry as the originator,
and TS 104-175 § 10.6.3 SHALL document this dual-use.

---

<a name="53"></a>
## 53. TS 104-175 § 10.5.7 — change detection vs server-stamped `modifiedAt`

**Hit:** Spec defines a change as "any of the members (including
children) in its corresponding JSON-LD node is updated with a value
different than the existing one." This includes system-managed
sub-attributes like `modifiedAt`.

An idempotent overwrite — same value, server stamps fresh
`modifiedAt` — would, on a literal reading, trigger a notification
because `modifiedAt` changed. That's a non-event the client doesn't
care about and creates notification storms.

**Spec:** TS 104-175 § 10.5.7, definition of "change". No carve-out for
server-stamped fields.

**Our call:** exclude system-managed temporal fields (createdAt,
modifiedAt, deletedAt) from the change-detection comparison. A
client-supplied `observedAt` change DOES trigger; a broker-stamped
`modifiedAt` alone does NOT.

**Fix wanted:** TS 104-175 § 10.5.7 should add: "Server-stamped system
Attributes (createdAt, modifiedAt, deletedAt) SHALL NOT
participate in change detection. A change is observed only on the
client-visible value(s) of the Attribute and its client-supplied
sub-Attributes."

---

<a name="54"></a>
## 54. TS 104-175 § 10.5.7 — `notification.attributes` IRI expansion: subscription context vs notification time

**Hit:** A Subscription is created with `notification.attributes:
["speed"]` and `jsonldContext: <ctx-v1>` (which maps speed → IRI X).
Later, the entity is updated; the entity's stored attribute is at
IRI X. The notification renders.

If the subscription's jsonldContext has been replaced (or, more
subtly, if a default core context has changed terms), should the
broker re-expand "speed" against the current context, or against
the context captured at subscription creation?

If re-expanded → consistency with current entity state at the cost
of subscription stability.
If creation-time → subscription stability at the cost of "speed"
possibly meaning a different IRI now.

**Spec:** TS 104-175 § 10.5.7 — "Term to URI expansion shall be observed
(clause 5.5.7)." TS 104-175 § 8.2.4 covers term-to-URI but doesn't address
this temporal stability question.

**Our call:** expand at subscription creation time; the resulting
IRI is stored on the Subscription. Re-expansion never happens.
Document the rule in `feedback_expand_canonical` memory.

**Fix wanted:** TS 104-175 § 10.5.7 should state: "Term-to-URI expansion of
`notification.attributes` SHALL be performed at Subscription
creation time using the Subscription's jsonldContext. The
resulting IRIs are stored with the Subscription and used verbatim
for matching and notification rendering thereafter."

---

<a name="55"></a>
## 55. TS 104-175 § 10.5.7 — deleted-entity notification + `notification.attributes` filter

**Hit:** Subscription has `notification.attributes: ["speed",
"color"]`. The watched entity is deleted. TS 104-175 § 10.5.7 says:

> If the notification was triggered by the deletion of an Entity
> and the notification.showChanges member is not set to true, only
> the deletedAt system property shall be provided.

But the sub's `notification.attributes` says only speed & color
should be returned. Is `deletedAt` returned because the trigger
overrides the filter? Or are speed/color returned alongside
`deletedAt`? Or neither?

**Spec:** TS 104-175 § 10.5.7, the quoted sentence implies the filter is
overridden — "only the deletedAt". But the sub's filter says
"speed, color" — explicit override or accidental contradiction?

**Our call:** trigger override — `deletedAt` always; attribute
filter ignored on delete-trigger notifications. The notification
body is `{id, type, deletedAt}` plus whatever the format
(concise/simplified/etc.) prescribes.

**Fix wanted:** TS 104-175 § 10.5.7 should state explicitly: "On an
entity-delete notification, `notification.attributes` is ignored;
the response contains only entity members (id, type) plus the
`deletedAt` sub-attribute. `notification.showChanges` true MAY
add the previously-stored attributes."

---

<a name="56"></a>
## 56. TS 104-175 § 10.5.7 — `notification.pick` / `notification.omit` vs entity-keyword members (id, type)

**Hit:** Subscription has `notification.omit: ["id"]`. Can the
broker actually omit `id`? `id` is the entity's identifier — every
NGSI-LD entity representation requires it. TS 104-175 § 10.5.7 says pick/omit
operate on "entity members listed". id/type ARE members.

Two readings:
- (a) entity keywords (id, type, @context) are excluded from
  pick/omit semantics; the param only operates on user attributes.
- (b) pick/omit is fully general; omitting `id` produces an entity
  representation that violates TS 104-175 § 5.2.6.4.1.

**Spec:** TS 104-175 § 10.5.7 / TS 104-175 § 7.3.2 — silent on this guard.

**Our call:** pick/omit cannot remove id, type, @context, scope. A
sub that tries (`omit=["id"]`) silently keeps id; no error. The
guard is implicit in the NGSI-LD entity representation requirement.

**Fix wanted:** TS 104-175 § 10.5.7 should state: "`notification.pick` and
`notification.omit` operate only on user-defined Attributes. The
entity-keyword members (id, type, @context, scope) are preserved
regardless." Or alternatively bless omit-id as a way to produce
attribute-only fragments (e.g. for downstream processing).

---

<a name="57"></a>
## 57. TS 104-175 § 10.5.2.4 / TS 104-175 § 10.5.3.4 — overlapping subscriptions: dedup or duplicate?

**Hit:** Client creates Subscription S1 watching attr X on entity
E, and Subscription S2 also watching attr X on E. Both send to the
SAME endpoint URL. One change to E.X triggers two notifications
(one per subscription).

Is that desired (each subscription is independent) or wasteful
(client gets duplicate data)? Should the broker dedup notifications
sharing identical endpoint + payload?

**Spec:** Each Subscription is independent; TS 104-175 § 10.5.2/2/3 say nothing
about cross-subscription dedup.

**Our call:** strict per-subscription notification. No dedup
across subscriptions, even with identical endpoint. Client is
responsible for not creating redundant subs.

**Fix wanted:** TS 104-175 § 10.5.7 should clarify: "Subscriptions are
independent; the Context Broker SHALL emit one notification per
matching subscription, even if multiple subscriptions match the
same change and share the same endpoint." That's the per-spec
reading and would make it explicit. Vendors offering dedup as an
extension should flag it as non-conformant.

---

<a name="58"></a>
## 58. TS 104-175 § 10.5 — subscription `expiresAt` reached while broker is down

**Hit:** Subscription has `expiresAt: T`. Broker is offline at T.
When the broker comes back at T+10s:

- Status field reads "active" (last persisted before T)? The
  resume code should detect expiration on load.
- Did the missed-interval timing matter? If sub was timeInterval-
  based, are missed intervals retroactively suppressed or do they
  fire on resume?
- Do throttling counters reset on resume?

**Spec:** TS 104-175 § 5.2.6.5.2 defines status with values active, paused,
expired. TS 104-175 § 10.5 doesn't address recovery state. The persisted-state
machine is undefined.

**Our call:**
- On startup, walk the sub cache; flip any sub with
  `expiresAt < now` to status=expired.
- Throttling counters persist; `lastSuccess` carries through.
- TimeInterval-based subs: missed intervals are not retroactively
  fired; next fire is `lastFire + interval`.

**Fix wanted:** TS 104-175 § 10.5 should add a "Subscription persistence and
recovery" sub-clause covering:
(a) post-restart status-recomputation rules,
(b) throttling counter persistence,
(c) timeInterval missed-fire policy (retroactive vs forward-only),
(d) pernot lastN evaluation across restarts.

Without this section, two compliant brokers behave differently
after a crash.

---

<a name="59"></a>
## 59. TS 104-175 § 5.2.4 — system Attributes: ambiguous set and rendering policy

**Hit:** Spec mentions "system Attributes" or "system properties"
in many places but doesn't enumerate the complete set in one
authoritative table. From scattered references we infer:

- `createdAt`, `modifiedAt`, `deletedAt`, `expiresAt` — entity-level
  and attribute-level
- `observedAt` — attribute-level
- `previousValue`, `previousObject`, `previousVocab`,
  `previousLanguageMap`, `previousJson` — only on
  notifications with `showChanges`
- `unitCode`, `lang` — sometimes called "system properties of
  attributes" but client-supplied

When `?sysAttrs=true` is set, which are returned? `createdAt`,
`modifiedAt`, `deletedAt`. But `expiresAt`? `observedAt`? Memory
fixture: ETSI tests assume sysAttrs=true also exposes `expiresAt`
but not `observedAt` (which is always returned regardless).

**Spec:** TS 104-175 § 5.2.4 introduces system attributes; TS 104-175 § 5.2.6.4.3/4.5.3 list
sub-attributes; TS 104-176 § 6.4.5 covers `sysAttrs` URL param. No single
authoritative list.

**Our call:** sysAttrs gate = { createdAt, modifiedAt, deletedAt,
expiresAt }. observedAt is always returned (client-supplied data,
not gate-controlled). unitCode / lang are user-supplied, always
returned. previousValue family only on notif+showChanges.

**Fix wanted:** TS 104-175 § 5.2.4 should have a normative table:
"NGSI-LD System Attributes" with three columns: (name, gate
controlling visibility, whether client-supplied or server-supplied).
Clears up `?sysAttrs=true` ambiguity in one place.

---

<a name="60"></a>
## 60. TS 104-175 § 7.3.2 — `pick` / `omit`: cross-parameter validation rules are incomplete

**Hit:** URL params `pick`, `omit`, and the legacy `attrs` interact
in non-obvious ways:

- `pick` AND `omit` together — does it AND (pick first, then omit
  from picked) or fail with 400?
- `pick` AND `attrs` — `attrs` is the deprecated alias for "pick+q"
  (per TS 104-175 § 12.3.3). Combined, contradictory?
- `omit` AND `attrs` — even more confused.
- Each accepts a comma-separated list of nested-path projection
  language (TS 104-175 § 7.3.2). Conflict on a path where pick says "keep X.Y"
  and omit says "drop X" — does X.Y survive?

**Spec:** TS 104-175 § 7.3.2 covers the projection language. TS 104-176 § 7.2.3 mentions
the URL params. Cross-parameter validation is implicit.

**Our call:** TS 104-176 § 7.2.3 / TS 104-175 § 7.3.2 violations → 400 BadRequestData
with ldParamsValidate diagnostics. Pick wins over omit on the
same path; deeper-path omit overrides pick on that sub-path.
`pick + attrs` and `omit + attrs` are both 400.

**Fix wanted:** TS 104-175 § 7.3.2 should add a validation matrix:
"Combinations of pick / omit / attrs are valid as follows: pick OR
omit OR attrs (not multiple). pick AND omit MAY be used together;
the resulting projection is { paths kept by pick } MINUS { paths
removed by omit }. Conflicts on overlapping paths: the more
specific path wins (longer JSONPath)."

---

<a name="61"></a>
## 61. TS 104-175 § 5.2.6.9 — query language `q` corner cases: precedence, escaping, null handling

**Hit:** `q=` is a mini-expression language for filtering. Several
under-specified corners:

- Operator precedence: `q=a==1;b==2|c==3` — does `;` bind tighter
  than `|`? Spec gives the operators but no precedence table.
- Negation: `!attr==value` vs `attr!=value` — equivalent? Both
  valid?
- String escaping inside `q`: how to match a value containing `;`,
  `|`, `==`? URL-encoding? Backslash? The spec doesn't say.
- Null matching: `q=attr==null` — matches attributes whose value
  is the JSON `null`? Or attributes with the NGSI-LD null marker
  `urn:ngsi-ld:null`? Or both?
- Type coercion: `q=year==2024` — year stored as string "2024"
  vs integer 2024 — match?

**Spec:** TS 104-175 § 5.2.6.9 defines the language but does not formalize
precedence, escaping, or coercion.

**Our call:** documented in `feedback_q_*` memories. Specifically:
`;` > `|` precedence; URL-encoding for special chars in values;
null matches only literal JSON null (the urn:ngsi-ld:null marker
is "attribute deleted" sentinel, not a queryable value).

**Fix wanted:** TS 104-175 § 5.2.6.9 should include:
(a) BNF grammar with explicit precedence,
(b) escaping rules for value strings containing operator chars,
(c) type-coercion table (string-vs-numeric comparison rules),
(d) null-matching semantics.

Without these, every broker's `q` parser is its own dialect.

---

<a name="62"></a>
## 62. TS 104-175 § 13 — `jsonldContext.kind` values are not enumerated

**Hit:** GET /jsonldContexts returns objects with a `kind` field.
We observe values from the codebase: `Cached`, `ImplicitlyCreated`,
`Hosted`, `ExplicitlyCreated`. Each maps to a different
provenance:

- `Cached` — broker downloaded it from a URL, holds a local copy
- `ImplicitlyCreated` — broker created it from a subscription/CSR
  body that carried inline @context
- `Hosted` — explicit POST /jsonldContexts to install
- `ExplicitlyCreated` — ??? (we have it in code, can't find spec
  reference)

The complete set, the state transitions, and when each is shown
are not in the spec.

**Spec:** TS 104-175 § 13 defines /jsonldContexts CRUD but does not
enumerate `kind` values or their semantics.

**Our call:** four kinds as listed above; documented in
`feedback_jsonld_context_kind` memory (informally).

**Fix wanted:** TS 104-175 § 13 should add: "The `kind` field of a
JSON-LD Context entry SHALL be one of: Cached, ImplicitlyCreated,
Hosted, ExplicitlyCreated. Each is defined as follows: ...".
Include the transitions (e.g. Cached entries with no recent use
may be evicted; Hosted are persistent until DELETE; etc.).

Without this, two brokers expose `kind` with diverging vocabulary
and clients can't reason about provenance portably.

---

<a name="63"></a>
## 63. TS 104-175 § 14 — EntityMap lifetime, eviction, and persistence

**Hit:** EntityMaps capture a paginated snapshot. They have a
default lifetime, eviction rule on resource pressure, and may
or may not persist across broker restarts.

Spec mentions a default lifetime ("5 minutes" widely cited) but
doesn't normatively pin it. Persistence behavior is undefined:
do EntityMaps survive a broker restart?

**Spec:** TS 104-175 § 14 defines EntityMap CRUD. The lifetime is
mentioned via the `expiresAt` field on the map. No section
defines:
- default lifetime when the request doesn't set expiresAt
- eviction policy when memory is full
- persistence across broker restart
- behavior on follow-up page request after expiry (404? auto-
  recreate? error?)

**Our call:** default lifetime = 5 minutes from creation;
in-memory only (does not survive restart); eviction on expiry
sweep (lazy, on next /entityMaps access). Client gets 404 if
the map expired between page requests.

**Fix wanted:** TS 104-175 § 14 should add:
(a) default lifetime (recommendation, RECOMMENDED 5 min),
(b) eviction policy MAY be implementation-defined but SHALL
preserve unexpired maps,
(c) persistence behavior MAY be in-memory; if so, post-restart
follow-up page request SHALL return 404 ResourceNotFound,
(d) recovery: the client MAY retry without entityMapId to
recreate.

---

<a name="64"></a>
## 64. TS 104-176 § 6.4.6 — `count` semantics under distributed federation

**Hit:** GET /entities?count=true gives a `NGSILD-Results-Count`
header alongside the (possibly limited) result page. Under
federation, the count is a federated count — the total entities
across all sources, after dedup, satisfying the filter.

But: the broker may not have fetched every entity. With limit=20,
the broker fetches at most 20 from each CSR. The real total may
exceed 20 × N CSRs. So the count is either approximate or requires
a separate "count-only" forwarded request to each CSR (count=true
without data).

**Spec:** TS 104-176 § 6.4.6 defines the count param + header for a single
broker. Federation is unaddressed.

**Our call:** issue count-only requests in parallel to each CSR
when `?count=true` is set; aggregate the per-source counts after
dedup. The count is *approximately* federated — exact only if
no overlap between sources (no entity appears in multiple CSRs).

**Fix wanted:** TS 104-176 § 6.4.6 should add:
"Under distributed federation, the count SHALL be the size of the
broker's merged result set after dedup. The Context Broker MAY
issue separate count-only forwarded requests to each Context
Source. In the presence of overlapping sources, the federated
count MAY be approximate; the Context Broker SHOULD include a
warning header NGSILD-Warning: 199 - federated count approximate."

---

<a name="65"></a>
## 65. TS 104-175 § 16.2.4 — snapshot capture from CSRs: do CSR-side changes after capture invalidate the snapshot?

**Hit:** A snapshot captures entity state at time T. Entities
sourced from a CSR are captured by querying the CSR at T. After T,
the CSR's data changes. A read against the snapshot at T+ should
return the captured T-state, not the live CSR state.

Two interpretations:
- (a) Snapshot stores a frozen copy. CSR changes after T are
  irrelevant — the snapshot is the authoritative T-state.
- (b) Snapshot stores entity ids + source pointers. A read at T+
  re-queries the CSR; CSR's then-current state is returned (or
  error if CSR no longer has the entity).

**Spec:** TS 104-175 § 16 defines snapshot semantics for the entity space.
The capture interaction with CSRs is silent.

**Our call:** option (a) — frozen copy. The snapshot has its own
storage tenant; capture fetches from CSR once, stores locally.
CSR-side mutations after T are not reflected. Memory:
`project_snapshot_storage`.

**Fix wanted:** TS 104-175 § 16 should explicitly state: "A snapshot is a
frozen copy of entity state at capture time T. Entities sourced
from Context Sources at capture time are stored in the snapshot;
subsequent changes at the original Context Sources do not modify
the snapshot. Reads against the snapshot return the captured
T-state, not the live CSR state."

---

<a name="66"></a>
## 66. TS 104-175 § 8.2.4 — IRI expansion of terms not defined in the active @context

**Hit:** Client sends `{ "speed": 42 }` but the active @context
has no mapping for `speed`. JSON-LD's `@vocab` fallback can
auto-construct an IRI (`<vocab>speed`) if `@vocab` is set; else
the term is bare. NGSI-LD layers its own rules on top: attribute
names must be IRIs per TS 104-175 § 5.2.6.4.3; bare terms violate that.

Three implementer reactions:
- (a) Reject with 400 — "Attribute name not in active @context".
- (b) Auto-construct via `@vocab` fallback if set; else 400.
- (c) Accept the bare term; treat its full name as the bare string
  for matching purposes (i.e. it stays "speed", not an IRI).

**Spec:** TS 104-175 § 8.2.4 / TS 104-175 § 5.3.2.1 reference JSON-LD's expansion rules.
Doesn't say what NGSI-LD does on a fallback miss. ETSI fixtures
sometimes assume (b), sometimes (a).

**Our call:** option (b) — fall back to `@vocab` when set;
otherwise the bare term is rejected with 400. Matches JSON-LD
default expansion + NGSI-LD's IRI requirement.

**Fix wanted:** TS 104-175 § 8.2.4 should add: "If a term in the request
cannot be expanded by the active @context (no direct mapping,
no applicable @vocab fallback), the Context Broker SHALL reject
the request with 400 BadRequestData and a ProblemDetails
referencing the unresolvable term."

---

<a name="67"></a>
## 67. TS 104-175 § 11.3.2 — `lastN` semantics across pagination and across distop

**Hit:** `?lastN=10` asks for the most recent 10 instances of each
attribute. Combined with:
- Pagination (limit + offset): which axis paginates first? The
  attribute axis (10 instances per attr, paginate over entities)
  or the time axis (paginate over time-slices, each containing
  up-to-lastN instances)?
- Distop fan-out: each CSR returns its own lastN=10. Broker
  merges. If a CSR returns 10 and the broker's local store has
  10, the merged set may have >10 — does the broker re-trim to
  lastN=10 after merge?

**Spec:** TS 104-175 § 11.3.2 / TS 104-175 § 7.2.6 / TS 104-175 § 8.5.2 define lastN, pagination,
multi-instance. The composition is not addressed.

**Our call:**
- lastN applies post-merge: each attribute's instance list is
  trimmed to most-recent N after distop merge.
- Pagination operates on the entity axis; within each entity,
  lastN governs the temporal-attribute axis. Combined: limit=L
  entities, each carrying lastN=N instances.
- Distop: forward lastN unchanged; trim locally after merge.

**Fix wanted:** TS 104-175 § 11.3.2 should add: "Under pagination, `lastN`
applies per-Attribute within each returned Entity; the pagination
parameter (`limit`) governs the entity dimension. Under
distributed federation, `lastN` SHALL be forwarded to each Context
Source and re-applied locally after merge."

---

<a name="68"></a>
## 68. TS 104-175 § 12.4 — CSR-Subscriptions: which CSR state changes trigger?

**Hit:** A csr-subscription watches the CSR space. Spec says it
fires when "a matching CSR is created, updated, or deleted". But
"updated" is broad:
- A counter update (timesSent++) — fires?
- An expiresAt change without other state — fires?
- A no-op write (PATCH with no actual change) — fires?
- Internal state-machine changes (cooldown engaged, lastFailure
  bumped) — fires?

We have:
- Active csr-subs in the catalog can hammer the notification
  endpoint on every distop forward (counter updates) if we fire
  on "any change to the CSR record".
- Or under-fire if we ignore everything except CRUD on the
  user-supplied fields.

**Spec:** TS 104-175 § 12.4 defines csr-sub CRUD but doesn't enumerate
trigger conditions in detail.

**Our call:** trigger only on user-visible CRUD: explicit POST,
PATCH (with field changes), DELETE. Counter updates and internal
state-machine transitions do not trigger. Same heuristic as
"observable user-supplied state changed".

**Fix wanted:** TS 104-175 § 12.4.6 should add: "A csr-subscription notification
SHALL be sent on Create / Patch (when at least one user-supplied
field changes) / Delete. Internal Context Source state changes
(distop counters, cooldown engagement, etc.) SHALL NOT trigger
csr-sub notifications."

---

<a name="69"></a>
## 69. TS 104-175 § 10.2.12 — Purge Entities and TRoE interaction

**Hit:** Purge Entities (`/entityOperations/purge`) removes
matching entities from the current-state store. Does it also
purge the TRoE temporal evolutions, or only the current state?

Two readings:
- (a) Purge is current-state only. The temporal evolution remains
  (a complete history of deleted entities).
- (b) Purge is total — both current-state AND temporal evolutions.
  Subsequent temporal queries return 404.

(a) is consistent with "soft delete" semantics in TRoE (createdAt
preserved; deletedAt added). (b) is "hard delete" matching the
purge name.

**Spec:** TS 104-175 § 10.2.12 defines Purge as removing entities. TS 104-175 § 11.2.7
(Delete Temporal Evolution) separately removes the temporal side.
The interaction isn't addressed.

**Our call:** Purge removes current-state only. TRoE evolution
survives. Clients wanting both must call Delete Temporal Evolution
separately (or use `?temporal=true` if we add such a flag).

**Fix wanted:** TS 104-175 § 10.2.12 should state: "Purge Entities operates
on the current-state store. The temporal evolution of the purged
entities (if any) is unaffected unless the URL parameter
`?temporal=true` is also supplied, in which case the temporal
evolution SHALL be purged with the same selector."

That way the spec defines both the default and the override.

---

<a name="70"></a>
## 70. TS 104-175 § 8.4.3 — merge-patch with NGSI-LD null markers in arrays / nested objects

**Hit:** Merge-patch (PATCH) uses `urn:ngsi-ld:null` as a tombstone
for attribute deletion. RFC 7396 (JSON Merge Patch) uses literal
JSON `null` for the same purpose. NGSI-LD diverges to accommodate
its data model.

Edge case: a PATCH body contains an array attribute and one
element is `"urn:ngsi-ld:null"`. Is the entire array deleted? Just
that element? Replaced with a null marker?

Similarly: a JsonProperty whose `json` is `{ "x": "urn:ngsi-ld:null" }`
— treated as "delete x from json"? Or preserved verbatim (the
opaqueness from entry 43)?

**Spec:** TS 104-175 § 8.4.3 defines merge-patch with the urn:ngsi-ld:null
marker semantics for attribute deletion. Element-level and
inside-opaque cases are silent.

**Our call:**
- Inside a JsonProperty `json` value: opaque (entry 43); the
  string `urn:ngsi-ld:null` is just a string, not a tombstone.
- Inside an array Attribute value (Property with array value):
  not a tombstone; the array is treated as a single value.
- Tombstone semantics apply only at the attribute level.

**Fix wanted:** TS 104-175 § 8.4.3 should add: "The `urn:ngsi-ld:null`
sentinel SHALL be interpreted as a deletion marker only at the
Attribute level (as a sibling of `type`, replacing `value` /
`object` / `languageMap` etc.). Occurrences within an Attribute's
value (array elements, nested objects, JsonProperty contents) are
preserved verbatim as data."

---

<a name="71"></a>
## 71. TS 104-175 § 5.2.6.5.6 — `contextSourceAlias` uniqueness enforcement across a federation

**Hit:** `contextSourceAlias` is the pseudonym used in `Via` for
loop detection (TS 104-175 § 10.6.3). The spec describes it as "a unique id
for a Context Source", but uniqueness is across what?

- Across the local broker's CSR cache?
- Across the entire federation?
- Across federations historically?

Two compliant brokers could pick `contextSourceAlias: "broker1"`
independently — both correct in isolation, fatal under federation
(false loops, missed loops).

**Spec:** TS 104-175 § 5.2.6.5.6 says alias is "non-empty string. Pseudonym
field as defined in IETF RFC 7230". RFC 7230 says pseudonyms
must be unique but doesn't say "across what". No NGSI-LD section
defines an allocation authority.

**Our call:** broker defaults the alias to `<exe-basename>:<port>`
to give a reasonable starting point. Multi-tenant: alias becomes
`<base>:<tenant>`. Admin override via `--csourceAlias` for
federation deployments. We rely on the admin to make it unique.

**Fix wanted:** TS 104-175 § 5.2.6.5.6 should add: "The contextSourceAlias
SHALL be unique across all Context Sources that may participate
in the same federation (i.e. that may issue or receive forwarded
requests with overlapping Via chains). The Context Broker
RECOMMENDED-default is `<broker-id>:<port>` or
`<broker-id>:<tenant>` for multi-tenant deployments. Operators are
responsible for resolving collisions across federation
boundaries."

Or: introduce a UUID-based default (`urn:uuid:...`) so collisions
are statistically impossible.

---

<a name="72"></a>
## 72. TS 104-175 § 5.2.6.5.6 — `contextSourceExtras` opaque JSON: same scope question as JsonProperty

**Hit:** `contextSourceExtras` is "JSON which shall not be
interpreted as JSON-LD using the supplied @context". Same
opaqueness contract as TS 104-175 § 5.2.6.4.10 JsonProperty.

The same scope question (entry 43) recurs:
- Inner `@context`, `@vocab`, prefixed names — preserve verbatim?
- Compaction on output — apply or skip?
- Validation — any rules (size limit, depth limit, character set)?

This is a small-surface field but appears on every broker's
/info/sourceIdentity response. Worth pinning.

**Spec:** TS 104-175 § 5.2.6.5.6 says "raw un-expandable JSON". Same gap as
entry 43.

**Our call:** byte-for-byte preservation. Parsed once at startup
(from `--contextSourceExtras` config file), cloned into each
response. No interpretation. Documented in memory.

**Fix wanted:** TS 104-175 § 5.2.6.5.6 should reference the same opaque-JSON
clause as TS 104-175 § 5.2.6.4.10 (the JsonProperty rules) — a single shared
"opaqueness contract" applied wherever NGSI-LD has opaque-JSON
fields. Cleaner than duplicating the rule per data type.

---

<a name="73"></a>
## 73. TS 104-175 § 5.2.6.6.1 — notification `cooldown` state machine

**Hit:** A subscription has `endpoint.cooldown: 5000`. The
notification endpoint fails. Cooldown engages: no further
notifications for 5s.

Underspecified:
- What COUNTS as a failure? Transport timeout? 4xx response? 5xx?
  Any non-2xx? An OK response with a parse failure?
- Does each successful notification reset cooldown?
- Does cooldown apply to the whole endpoint, or per-subscription
  even if multiple subs share the endpoint URL?
- Is cooldown observable to the client (a sub-stats field
  "cooldownUntil")?
- After cooldown expires and the next attempt also fails, does
  cooldown re-engage immediately (exponential? linear?) or wait
  for the full 5s again?

**Spec:** TS 104-175 § 5.2.6.6.1 defines cooldown as a number. TS 104-175 § 10.5.7 mentions
"If requests are received before the cooldown period has expired,
no notification is sent." Behaviour of the state machine itself
is unstated.

**Our call:** any non-2xx triggers cooldown; cooldown is per-
subscription (not per-endpoint); fresh failure after expiry
restarts the same cooldown (no exponential backoff); cooldown
state is observable via the sub's lastFailure field.

**Fix wanted:** TS 104-175 § 5.2.6.6.1 should add a cooldown state-machine
sub-clause:
- (a) Failure definition: ANY non-2xx response, transport error,
  or parse-failure on the response counts.
- (b) Scope: per-subscription (not per-endpoint).
- (c) Re-engagement: each failure restarts a full cooldown
  (no exponential), or implementations MAY exponential-backoff
  with a documented capacity.
- (d) Observability: SHALL surface lastFailure timestamp on
  subscription status reads.

---

<a name="74"></a>
## 74. TS 104-175 § 5.2.6.4.6 — LanguageProperty simplified rendering without `?lang=`

**Hit:** GET /entities/{id}?format=simplified against an entity
whose `name` attribute is a LanguageProperty:
`{ "type": "LanguageProperty", "languageMap": { "en": "Cat",
"de": "Katze" } }`.

In simplified form, a Property collapses to `{ "name": "value" }`.
For LanguageProperty, the simplified form is:
- without `?lang=` URL param: ???
- with `?lang=en`: `{ "name": "Cat" }`

Spec gives the second case but is silent on the first. Three
implementer reactions:
- (a) Return the entire languageMap object: `{ "name": { "en":
  "Cat", "de": "Katze" } }` (lossless but not "simplified").
- (b) Return a default language entry: `{ "name": "Cat" }`
  picking some canonical language (en? @none?). Lossy.
- (c) Reject with 400 — simplified+LanguageProperty requires
  `?lang=`.

**Spec:** TS 104-175 § 5.2.6.4.6 + TS 104-175 § 5.3.2.4 (simplified) — no joint rule. Per
memory `feedback_lang_required_simplified` we chose (c) and ETSI
fixtures generally agree.

**Our call:** option (c). Simplified LanguageProperty without
?lang= raises 400 BadRequestData with detail naming the
LanguageProperty's attribute name.

**Fix wanted:** TS 104-175 § 5.2.6.4.6 + TS 104-175 § 5.3.2.4 should explicitly say:
"Simplified representation of an Entity containing a
LanguageProperty SHALL include a `?lang=` URL parameter. In its
absence, the Context Broker SHALL respond with 400 BadRequestData."

Or pick (a) lossless or (b) default-language as the spec answer.
Any explicit answer beats the current silence.

---

<a name="75"></a>
## 75. TS 104-175 § 5.3.2.7 — aggregated temporal representation × sysAttrs

**Hit:** A temporal query with `?aggrMethods=avg,sum` returns
aggregated values per period (TS 104-175 § 5.3.2.7). System Attributes
(createdAt, modifiedAt) are timestamps on individual instances,
not aggregable.

When `?sysAttrs=true` is combined with aggregation:
- Are createdAt/modifiedAt returned per-period (the timestamp of
  the FIRST or LAST instance in the period)?
- Are they returned at the attribute level (timestamp of the
  whole attribute series)?
- Are they suppressed entirely under aggregation?

**Spec:** TS 104-175 § 5.3.2.7 + TS 104-176 § 6.4.3 cover aggregation. The sysAttrs
interaction is silent.

**Our call:** sysAttrs are suppressed under aggregation. The
response contains only the aggregated metrics (avg/sum/min/max
etc.) per period. createdAt / modifiedAt do not appear.

**Fix wanted:** TS 104-175 § 5.3.2.7 should add: "When aggregated
representation is combined with `?sysAttrs=true`, system
Attributes (createdAt, modifiedAt, deletedAt) SHALL be omitted
from the response. Aggregation operates on the value series
only; per-instance system metadata is not aggregable and SHALL
NOT be surfaced in aggregated form."

---

<a name="76"></a>
## 76. TS 104-176 § 6.4.9 — `NGSILD-Snapshot` header: applicable resources and inheritance

**Hit:** `NGSILD-Snapshot: <id>` directs reads to a snapshot.
Applicable to GET /entities, GET /entities/{id}, GET /types, GET
/attributes. But:
- Distop forwards: does the broker forward `NGSILD-Snapshot` to
  CSRs?
- Subscription notifications: a sub created against a snapshot —
  does each notification carry the header?
- Mixed-mode requests: `NGSILD-Snapshot` + `?local=true` — should
  one override the other?
- Header on writes: explicitly forbidden? Silently ignored?

**Spec:** TS 104-176 § 6.4.9 defines the header. The interaction matrix
above isn't pinned.

**Our call:**
- Forward `NGSILD-Snapshot` to CSRs verbatim during distop. CSRs
  that don't support snapshots return 404 / 400; broker treats
  that as "no contribution".
- Subscription created via a snapshot uses the snapshot's frozen
  state for matching; notifications include the snapshot id.
- `?local=true` + snapshot: snapshot reads are always local (the
  snapshot itself is a local frozen copy), the param is a no-op.
- Header on writes: in 1.9.1, writes against a snapshot are not
  defined (see entry 86); we reject with 405 today. Future
  revisions are expected to bless snapshot writes via the same
  CRUD API + header.

**Fix wanted:** TS 104-176 § 6.4.9 should add an interaction matrix:

| context        | NGSILD-Snapshot behaviour                                          |
|----------------|--------------------------------------------------------------------|
| reads          | applies — return snapshot state                                    |
| writes         | undefined in 1.9.1 (see entry 86); broker SHOULD reject with 405   |
| distop forward | forward the header verbatim                                        |
| subscription   | snapshot-id captured at creation; notifications carry it           |
| ?local=true    | no-op (snapshots are local)                                        |

---

<a name="77"></a>
## 77. TS 104-175 § 9.4 — loop detection: `Via` header parsing corner cases

**Hit:** Loop detection compares the broker's own alias against
entries in the inbound `Via` header chain. Several parsing
ambiguities:

- Case sensitivity: is `Broker1` equivalent to `broker1`? RFC 7230
  says Via tokens are case-insensitive; NGSI-LD TS 104-175 § 10.6.3 says
  pseudonyms compare per RFC 7230 but doesn't repeat the case
  rule.
- Whitespace: `Via: 1.1 broker1, 1.1 broker2` vs `Via: 1.1
  broker1,1.1 broker2` — both valid HTTP, broker must tolerate.
- Multiple Via headers vs single comma-separated: HTTP allows
  either; both must collapse to the same chain.
- Maximum Via depth: a deep federation with many hops generates
  a long Via. Spec sets no limit; some HTTP frameworks cap header
  size and silently truncate.
- Pseudonym with embedded spaces or commas — not a valid token but
  some operators set them anyway. Reject? Sanitize?

**Spec:** TS 104-175 § 10.6.3 / TS 104-175 § 9.4 reference RFC 7230. Don't restate the
parsing rules.

**Our call:**
- Case-insensitive comparison (per RFC 7230).
- Tolerant whitespace; multiple Via headers and comma-separation
  both supported.
- No depth limit on our side; rely on HTTP framework cap and
  emit 431 Request Header Fields Too Large if hit.
- Invalid tokens (spaces, control chars) → 400 BadRequestData.

**Fix wanted:** TS 104-175 § 9.4 should add a "Via parsing" sub-clause
restating the relevant RFC 7230 rules and pinning:
(a) case-insensitive pseudonym comparison,
(b) tolerant whitespace,
(c) one-or-many Via header forms equivalent,
(d) recommended max depth (e.g. 8) before rejecting with 508
  Loop Detected as a defensive measure,
(e) malformed pseudonym → 400.

---

<a name="78"></a>
## 78. TS 104-175 § 8.6 — `?local=true` semantics across endpoints

**Hit:** `?local=true` is named for "skip the distop dispatcher;
local data only". It's documented for the read side (GET
/entities). What about:

- POST /entities — does `?local=true` skip the forward to
  exclusive CSRs (and lose data)? Or is the param a no-op on
  writes?
- DELETE /entities/{id} — local delete only?
- POST /subscriptions — create a sub that only watches local
  entities, never federates?
- /info/sourceIdentity — meaningless?

We've implemented `?local=true` for the major read routes.
Write-side behaviour is implementer policy.

**Spec:** TS 104-175 § 8.6 defines `?local`. Reads are documented; writes
are implicit/silent.

**Our call:** `?local=true` on writes does NOT skip the exclusive
chop (data must not go missing). Inclusive forwards: skipped.
Sub creation: silently ignored (subs always observe local
mutations; federation is implicit). Sourceidentity: ignored.

**Fix wanted:** TS 104-175 § 8.6 should add a behaviour matrix:

| operation                  | local=true effect                                |
|----------------------------|--------------------------------------------------|
| read                       | skip dispatcher; return broker's own data only   |
| write (exclusive coverage) | error 409 — can't write what we don't own        |
| write (inclusive coverage) | skip forward; apply locally only                 |
| sub create                 | ignored (subs are always local-observing)        |
| /info/*                    | ignored                                          |

---

<a name="79"></a>
## 79. TS 104-175 § 8.5.2 — multi-instance batch with two entries for the same `entityId`

**Hit:** POST /entityOperations/create with body
`[ { id: urn:X, type: T, a: ... }, { id: urn:X, type: T, b: ... } ]`
— two array elements addressing the same entity in the same batch.

Possible interpretations:
- (a) Reject as conflicting — 400 BadRequestData.
- (b) Apply in array order; second is "Already Exists" because
  first created the entity. Response has notCreated[1].
- (c) Merge into one logical create with both a and b.

ETSI fixtures use (b). Our implementation also uses (b).

**Spec:** TS 104-175 § 8.5.2 covers batch multi-instance semantics for
*Attribute instances* (same entityId + attrName + different
datasetId). The same-entityId-distinct-elements case is silent.

**Our call:** option (b). Each array element is its own
operation; later elements may fail with AlreadyExists.

**Fix wanted:** TS 104-175 § 8.5.2 should add: "If a batch operation
contains multiple array elements addressing the same Entity ID,
each element is processed in array order. Subsequent elements MAY
fail with AlreadyExists (createEntity) or with their respective
error type (other ops). The Context Broker SHALL NOT merge
multiple same-id elements into a single logical operation."

(Or pick (a) or (c) — but pick something.)

---

<a name="80"></a>
## 80. TS 104-175 § 5.2.6.4.8 / TS 104-175 § 5.2.6.4.9 — ListProperty / ListRelationship simplified form

**Hit:** ListProperty/ListRelationship are arrays of values/objects
under `valueList`/`objectList`. Simplified form drops type and
returns a bare value.

For a regular Property: simplified is `{ attr: value }`.
For a ListProperty: simplified is `{ attr: [v1, v2, v3] }` — the
list values are the bare array.

For a Relationship: simplified is `{ attr: targetId }`.
For a ListRelationship: simplified is `{ attr: [t1, t2, t3] }`.

But what about MIXED — a ListProperty whose elements have
metadata (per-element observedAt)? Lossy or rejected?

**Spec:** TS 104-175 § 5.2.6.4.8 / TS 104-175 § 5.2.6.4.9 define List types. TS 104-175 § 5.3.2.4
(simplified) doesn't enumerate the projection rule per type.

**Our call:** simplified ListProperty = bare array of values; any
per-element metadata is dropped. Same for ListRelationship.

**Fix wanted:** TS 104-175 § 5.3.2.4 should have a projection table:

| attribute type   | simplified projection                            |
|------------------|--------------------------------------------------|
| Property         | `value`                                          |
| Relationship     | `object`                                         |
| LanguageProperty | language-tag value (requires `?lang=`)           |
| GeoProperty      | `value` (GeoJSON Geometry)                       |
| ListProperty     | `valueList` (bare array)                         |
| ListRelationship | `objectList` (bare array of object URIs)         |
| VocabProperty    | `vocab`                                          |
| JsonProperty     | `json` (opaque)                                  |

Today this table is reconstructed by every implementer from
scattered hints.

---

<a name="81"></a>
## 81. TS 104-175 § 15.2.4 — `/info/sourceIdentity` per-tenant variation

**Hit:** GET /info/sourceIdentity returns the broker's identity.
In a multi-tenant deployment, the same broker serves N tenants.
Should the response vary per tenant (different
contextSourceAlias, different contextSourceExtras), or be uniform?

We argue per-tenant (alias differs because TS 104-175 § 5.2.6.5.6 explicitly
mentions multi-tenancy). But contextSourceExtras may be admin-
configured per-tenant or globally — spec doesn't say.

`contextSourceUptime` — broker uptime or tenant uptime (time since
the tenant was first written to)? The latter requires state
beyond the process.

**Spec:** TS 104-175 § 5.2.6.5.6 + TS 104-175 § 15.2.4 mention multi-tenancy but don't
explicitly define per-tenant behaviour for each field.

**Our call:**
- `contextSourceAlias`: per-tenant (`<base>:<tenant>`).
- `contextSourceUptime`: process uptime (broker-wide; same for
  all tenants).
- `contextSourceTimeAt`: now() (broker-wide).
- `id`, `type`: broker-wide.
- `contextSourceExtras`: broker-wide today; could be per-tenant
  via additional config.

**Fix wanted:** TS 104-175 § 5.2.6.5.6 + TS 104-175 § 15.2.4 should specify per-field
which axis varies. Cleanest:

| field               | per-tenant?                            |
|---------------------|----------------------------------------|
| id                  | no (broker-wide)                       |
| type                | no                                     |
| contextSourceAlias  | YES (per-tenant)                       |
| contextSourceUptime | no (broker-wide process uptime)        |
| contextSourceTimeAt | no                                     |
| contextSourceExtras | MAY be per-tenant (admin choice)       |

---

<a name="82"></a>
## 82. TS 104-175 § 9.2 — CSR `operations` list: named-group + literal-op mixing and typo handling

**Hit:** A CSR's `operations` array can contain literal op names
(`createEntity`, `retrieveEntity`) AND named group aliases
(`federationOps`, `redirectionOps`, etc.). Edge cases:

- Mixed: `operations: ["federationOps", "createEntity"]` — does
  this UNION the group's ops with `createEntity` (which isn't in
  federationOps)? Or is mixing illegal?
- Typo: `operations: ["federtionOps"]` (note the typo) — does the
  broker reject (400)? Accept as a literal op name (silent
  failure — no op matches)?
- Empty array: `operations: []` — same as "no ops" (the CSR
  responds to nothing) or same as "default" (federationOps)?
- Default (field absent): TS 104-175 § 9.2 says "the default set of
  operations matches the group defined as federationOps". Explicit
  `operations: ["federationOps"]` and absent should be equivalent —
  but the broker may treat them differently on the wire (e.g. PATCH
  semantics, "operations was set to default" vs "operations was
  explicitly federationOps").
- Group overlap: `operations: ["federationOps", "redirectionOps"]`
  — these groups overlap on retrieveEntity, queryEntity, etc.
  Union or set-of-sets?

**Spec:** TS 104-175 § 9.2 lists Table "Names of implemented Operations"
(individual ops) and Table "Named Operation Groups" (named groups). The
combinatorics aren't pinned.

**Our call:**
- Mixed: union (group expands, then literal ops add).
- Typo: silent failure (unknown op name is a no-op for matching).
  Should probably be 400 — log warning today.
- Empty `[]`: same as "no ops match"; CSR is effectively inert.
- Field absent: federationOps default.
- Overlap: union with dedup.

**Fix wanted:** TS 104-175 § 9.2 should add: "The `operations` list MAY
contain a mix of individual op names (Table "Names of implemented
Operations") and named group aliases (Table "Named Operation Groups").
The effective op set is the union.
Unrecognised names SHALL be rejected with 400 BadRequestData at
CSR creation / patch time. An empty array SHALL mean the CSR
supports no operations. An absent `operations` field SHALL imply
the `federationOps` default."

---

<a name="83"></a>
## 83. TS 104-175 § 7.2.6 — temporal `before` / `after` / `between` bound asymmetry

**Hit:** TS 104-175 § 7.2.6 defines bound inclusivity per relation:
- `before` — comparison value is **exclusive**
- `after` — comparison value is **inclusive**
- `between` — lower bound **inclusive**, upper bound **exclusive**

So at exactly `timeAt = T`:
- `?timerel=before&timeAt=T` does NOT match samples at T.
- `?timerel=after&timeAt=T` DOES match samples at T.
- `?timerel=between&timeAt=T&endTimeAt=T'` — at T matches, at T'
  doesn't.

This is asymmetric. A client who wants the COMPLEMENT of "before
T" cannot use "after T" — both exclude samples at T from "before"
but include them in "after" (because of the asymmetry, querying
union(before, after) over the same T = matches every sample EXCEPT
those at T are matched twice, those at T match once).

Worse: there's no way to express "at or before T" — `before` is
exclusive, no inclusive variant.

**Spec:** TS 104-175 § 7.2.6, verbatim — "before" exclusive, "after"
inclusive, "between" half-open `[T, T')`.

**Our call:** implement literally. Document the asymmetry in user
docs.

**Fix wanted:** TS 104-175 § 7.2.6 should pick ONE policy and apply
consistently. Two clean options:
- (a) All-inclusive: before, after, between(closed-closed). Union
  of complementary queries returns everything; intersections at
  the boundary double-count.
- (b) All half-open lower-inclusive (matches `between`):
  `before T` = strict less-than, `after T` = greater-or-equal,
  `between T T'` = `[T, T')`. Symmetric, no overlap, no gap.

Option (b) is what most temporal DBs use and what `between` already
mimics. Aligning `before`/`after` with it removes a real
implementer trap and a subtle off-by-one for clients.

---

<a name="84"></a>
## 84. TS 104-175 § 7.6 — `?orderBy=` under distop: silent ignore, warn, or 400?

**Hit:** TS 104-175 § 7.6 explicitly says:

> Sort ordering is never applied to distributed operations, and the
> defined sort ordering strategy may depend on implementation
> specific configurations.

This is correct in principle — proper ordering across a federation
of independent sources can't be done without first collecting the
full result set (which is exactly what a snapshot does, by design).
The spec's intent is right.

What's left underspecified: when a client sends
`?orderBy=foo` and distop is in effect, what should the broker do?

- **(a)** Silently ignore `orderBy`. Return entities in
  implementation-defined order (we use `createdAt` ascending —
  it's stable, deterministic per source, and recoverable from the
  local DB without extra cost). The client gets results but the
  ordering hint was lost without warning.
- **(b)** Honour `orderBy` only when the client has scoped the
  request locally: `?local=true` OR `NGSILD-Snapshot: <id>`.
  Otherwise ignore.
- **(c)** Reject `?orderBy` under distop with 400 BadRequestData
  ("ordering across distributed sources is not supported, use
  `?local=true` or a snapshot").
- **(d)** Best-effort: sort the per-source results locally, but
  document that the combined result is not globally ordered.

Our broker takes (b) — orderBy is honoured only with `?local=true`
or a snapshot. Without those, the default order is `createdAt`
ascending and `?orderBy` is silently ignored.

**Spec:** TS 104-175 § 7.6 explicitly excludes orderBy from distop but
doesn't say what the broker should do with the URL param when the
client supplies it anyway.

**Our call:** option (b) plus default `createdAt`-asc.

**Fix wanted:** TS 104-175 § 7.6 should add:

> Under distributed operations the Context Broker SHALL ignore the
> `?orderBy=` URL parameter. To request ordering across the
> federated result set, the client SHALL either set `?local=true`
> (restricting the query to the broker's own data) or use a
> snapshot (see clause 5.16). The default order of entities in a
> distributed response is implementation-defined; brokers
> RECOMMENDED-default to `createdAt` ascending. When `?orderBy=`
> is supplied AND distop is in effect, the broker SHOULD include
> the response header `NGSILD-Warning: 110` (or equivalent) to
> signal that the ordering hint was ignored.

The "snapshot for federated ordering" pattern — which the spec
implicitly introduces with TS 104-175 § 16 — should also be explicitly
cross-referenced from TS 104-175 § 7.6 as the **right** way to get
ordered+paginated federated reads.

---

<a name="85"></a>
## 85. TS 104-175 § 7.6.2.2 — datatype comparison order: strings before dates, where does GeoProperty fit?

**Hit:** TS 104-175 § 7.6.2.2 specifies a comparison order for mixed-type
attribute values:

> Numbers, Strings, Object, Array, Boolean, Time, Date, DateTime,
> Null, Attribute does not exist

Two surprises:
- Strings come BEFORE Date / Time / DateTime. So
  `"2024-01-01T00:00:00Z"` as a string compares BEFORE the same
  value as a DateTime. The string representation of a date is
  ranked below the DateTime representation — but only if the
  string and DateTime are present *as different attributes* in
  the same sort. The intuitive ordering would group all
  date-shaped values regardless of declared type.
- GeoProperty values, VocabProperty values, LanguageProperty
  values — where do they fit in this list? "Object" bucket?

When two entities have an attr declared GeoProperty (sort by
value, not distance), which side wins in mixed-type sort with a
String-valued entity for the same attr? Spec doesn't say.

**Spec:** TS 104-175 § 7.6.2.2 lists the order as cited above. Doesn't
enumerate Geo/Vocab/Language values.

**Our call:**
- GeoProperty value → "Object" bucket (it's a GeoJSON object).
- VocabProperty value → "String" bucket (vocab values are strings).
- LanguageProperty value → "Object" bucket (languageMap is an
  object).
- ListProperty value → "Array" bucket.
- JsonProperty value → bucket by the json's top-level type (object
  or array; rarely scalar).

**Fix wanted:** TS 104-175 § 7.6.2.2 should add: "For NGSI-LD attribute value
types: GeoProperty values are compared as Object; VocabProperty
values as String; LanguageProperty values (without `?lang=`) as
Object, with `?lang=` as the resolved-language String;
ListProperty values as Array; JsonProperty values by their
top-level JSON type." And reconsider the "Strings before
DateTime" ordering — it produces non-intuitive sorts for
real-world data where date-shaped strings are common.

---

<a name="86"></a>
## 86. TS 104-176 § 6.4.9 + TS 104-175 § 16 — writes against `NGSILD-Snapshot` are undefined in 1.9.1

**Hit:** A snapshot is effectively a tenant-shaped, isolated
data view. The NGSI-LD CRUD API (POST /entities, PATCH, DELETE,
…) naturally extends to operate on that tenant — just by setting
the `NGSILD-Snapshot: <id>` header on the request. No new
endpoints needed.

But 1.9.1 only *describes* this header for reads (TS 104-176 § 6.4.9).
Writes are neither blessed nor explicitly forbidden:

- POST /entities + `NGSILD-Snapshot: foo` — the spec doesn't say
  what happens. Broker decision space:
  - (a) Reject with 405 Method Not Allowed.
  - (b) Reject with 501 Not Implemented.
  - (c) Ignore the header and write into the default tenant
    (silent data leak into the wrong place — terrible).
  - (d) Treat as snapshot mutation (the future-revision behaviour).
- Same question for PATCH, DELETE, subscription CRUD, CSR CRUD
  with the header.

Future revisions of NGSI-LD are expected to bless snapshot writes
via this same header pattern — turning snapshots into mutable
sub-tenants. Without an explicit prohibition in 1.9.1, the version
boundary between "snapshots are read-only" and "snapshots are
writable" is invisible to clients.

**Spec:** TS 104-176 § 6.4.9 describes the header on reads. TS 104-175 § 16 describes
snapshot CRUD on the Snapshot object itself (its metadata, status,
queries). Neither addresses writes into the snapshot's data.

The implicit read-only-ness comes from TS 104-175 § 16.5 only allowing
status updates ("Update Snapshot Status") and TS 104-175 § 5.2.6.5.4 marking
`snapshotQueries` / `snapshotTemporalQueries` as read-only after
creation. The *data* read-only-ness is by absence of mention.

**Our call:** option (a). POST/PATCH/DELETE with
`NGSILD-Snapshot:` returns 405 today. When the spec adds snapshot
writes (a future revision), the broker drops the 405 and routes
the write into the snapshot tenant. The broker exposes the spec
version it implements via `/info/sourceIdentity`.

**Fix wanted:** TS 104-176 § 6.4.9 should add explicitly: "In NGSI-LD
1.9.1, the `NGSILD-Snapshot` header is defined only on Read
operations (GET, query). A Context Broker receiving the header
on a non-Read operation SHALL reject the request with 405 Method
Not Allowed. Future revisions of the present document MAY define
write operations against snapshots using the same header
pattern."

That sentence (a) makes the read-only-ness explicit in 1.9.1 and
(b) reserves the API surface for the future-revision uplift —
so clients can rely on a version-aware feature negotiation
without ambiguity.

---

<a name="87"></a>
## 87. TS 104-175 § 5.2.6.5.2 / TS 104-175 § 10.5 — Subscription has no `datasetId` filter for multi-instance attributes

**Hit:** Building a DDS-action client on top of NGSI-LD, the broker
materialises each in-flight action goal as a per-goal `datasetId`
instance of the action-tied attribute (e.g. `fib` with
`datasetId: "urn:goal:<uuid>"`). Each notification from the action
server (status / feedback / result) lands as an update on that
specific instance. The client triggering goal G1 wants notifications
for G1's lifecycle — not the chatter from every other client's
goals on the same attribute. A subscription on `watchedAttributes:
["fib"]` is too coarse: it fires for *every* instance.

The same shape recurs anywhere multi-instance is used as a per-job /
per-stream / per-sensor-channel discriminator: telematics with one
attribute per vehicle multi-instance, IoT sensors batching readings
under a single attribute keyed by datasetId, federated job-queue
broker patterns. In every case "subscribe to my instance" is the
natural ask and there is no spec-blessed way to express it.

**Spec:** Subscription (TS 104-175 § 5.2.6.5.2) supports `entities[]` (id, idPattern,
type), `watchedAttributes`, `q`, `geoQ`, `scopeQ` — all at entity /
attribute / value level. None descend to the instance level. TS 104-175 § 10.5
(Subscription operation) describes change detection in terms of
"Attribute" semantics with no mention of how the matcher should
behave when an attribute has N instances.

The spec is silent on:

- whether a subscription fires per-instance or per-attribute
  (one notification per change vs one coalesced per request);
- whether the notification body carries just the changed instance
  or the full instance array;
- how a subscriber expresses "only this datasetId" interest.

Implementations have to pick: most fire one notification per
write irrespective of instance, with the body carrying the entity
in normalised form (full instance array). This is correct under the
"watchedAttributes is attribute-level" reading but is operationally
unusable for streaming-state-machine use cases.

**Our call:** Going to add a broker-local extension: optional
`datasetId` (string or array) alongside `watchedAttributes` in the
subscription payload. When present, the subscription matcher checks
the altered-instance's datasetId against the filter and skips when
no match. Carries through to `OrionldAttributeAlteration` (new
`datasetId` field) so `subCacheAlterationMatch` can filter cheaply.
Notification body still carries the whole entity; the filter only
gates *whether* a notification fires.

**Fix wanted:** Add `datasetId` (string or array of strings, optional)
as a filter member on Subscription, scoped per `watchedAttributes`
entry — or as a sibling to `watchedAttributes` if the same filter
applies across all watched attributes (the two shapes have different
ergonomics for the multi-attribute case; spec should pick one).

While at it, clarify:

1. whether a single update to an attribute that touches one instance
   produces one or many notifications (per-instance or coalesced);
2. notification body when an attribute is multi-instance: full
   array vs only the changed instance (or both, via `notification.format`);
3. interaction with `notification.pick` / `notification.omit` /
   `notification.attributes` — does an attribute-level pick imply
   "all instances of that attribute" or can the pick descend?

Concrete use cases that need this clarification: streaming actions
(goal datasetId), per-sensor batched readings, per-job state machines.

---

<a name="88"></a>
## 88. TS 104-175 § 10.5.7 — Subscription trigger conditions are whole-attribute; no per-instance Create / Update / Delete

**Hit:** Companion problem to [[#87]]. Once a subscription *can*
target a specific datasetId (or the multi-instance attribute in
general), subscribers also want to distinguish the **kind** of
event at instance granularity:

- a new dataset instance appeared on this attribute
  (`AttributeInstanceCreate`);
- an existing dataset instance's value or sub-attrs changed
  (`AttributeInstanceUpdate`);
- a dataset instance disappeared (`AttributeInstanceDelete`).

Today's trigger taxonomy is `AttributeAdded` / `AttributeValueChanged`
/ `AttributeModifiedAtChanged` / `AttributeDeleted` — all
*whole-attribute*. Multi-instance churn collapses into
`AttributeValueChanged` regardless of whether the change was the
appearance of a brand-new datasetId or just a value tweak on an
existing one. Action / job clients want to react to "my goal
appeared" exactly once and "my goal is finished" exactly once,
not on every intermediate envelope churn.

**Spec:** TS 104-175 § 10.5.7 (Subscription change-detection) defines
notification triggering in terms of `Attribute` events. Multi-
instance behaviour is implicit and the trigger labels don't
mention it. Subscribers can't ask "fire only on a new
instance appearing" today.

**Our call:** With [[#87]]'s `datasetId` subscription filter, half
the use case is already covered (per-instance dispatch is
possible). Distinguishing the *kind* of event still needs
per-instance trigger conditions — planning to add
`AttributeInstanceCreate`, `AttributeInstanceUpdate`,
`AttributeInstanceDelete` as optional values in our broker's
subscription `trigger` field, alongside the existing
whole-attribute set. Default behaviour (no trigger specified)
remains the current "all changes" semantic.

**Fix wanted:** Add the per-instance trigger conditions to
TS 104-175 § 10.5.7's enumerated set. Clarify the relationship to whole-
attribute triggers — e.g. does subscribing to
`AttributeInstanceUpdate` ALSO match a value change on the
default (no-datasetId) instance? Likely yes (the default
instance is just "no datasetId" in the spec's data model),
but the wording needs to make that explicit.

Bundles well with [[#87]] — a Subscription that has both a
`datasetId` filter and an `AttributeInstanceUpdate` trigger is
exactly the streaming-action subscriber's natural ask.

---

<a name="89"></a>
## 89. TS 104-175 § 7.2.3 — q-value grammar requires quotes around strings, but real-world clients (and the ETSI suite) send unquoted URIs

**Hit:** the TS 104-175 § 7.2.3 grammar for q value is roughly
```
QueryValue = String / Number / DateTime / TimeStamp / Range / List
String     = """ *char """      ; must be quoted
```
so `locatedIn=="urn:ngsi-ld:City:Pisa"` is well-formed, but
`locatedIn==urn:ngsi-ld:City:Pisa` (no quotes) is **not** —
the value is grammatically required to be Number / DateTime /
etc. when not quoted.

In practice:

- The ETSI conformance suite (019_09_08, 019_09_10, and
  numerous others) sends URIs **unquoted** in q.
- orionld accepts unquoted strings as a lenient convenience.
- Strict parsers (like the swBroker parser before this change)
  fell through to `strtod`, returning 0.0 for any non-numeric
  unquoted value — and the q never matched.

**Spec:** TS 104-175 § 7.2.3 grammar is strict. TS 104-175 § 7.2.3 prose is silent on
lenient parsing.

**Our call:** when an unquoted value doesn't fully consume as a
number (range, datetime, list), accept the raw bytes as a
string. The implementation requires `strtod` to consume the
entire token to count as numeric; anything left over flips the
term to `LdQString`. Boolean/datetime/range/list paths run
first and are unaffected.

**Fix wanted:** the spec should pick one:

- **Tighten** TS 104-175 § 7.2.3 prose: brokers SHALL reject unquoted
  non-numeric values with 400 BadRequestData. The conformance
  suite is then wrong and must quote URIs.
- **Loosen** TS 104-175 § 7.2.3 grammar: unquoted string is a valid q value
  shape, with the same character class as quoted (URI-safe
  chars + `:`, `_`, `-`, etc.). Brokers SHALL accept.

Either is fine; the current "grammar says one thing, suite
tests another" gap is the actual problem. We've taken the
lenient interpretation because the suite already depends on
it.

---

## Template for new entries

```
## N. § X.Y.Z — one-line summary

**Hit:** what we ran into.

**Spec:** what the spec says (or doesn't).

**Our call:** what we implemented.

**Fix wanted:** what we want clarified.
```
