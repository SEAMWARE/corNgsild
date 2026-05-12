# NGSI-LD Spec Doubts & Gaps

Running list of ambiguities, silent cases, and genuinely contradictory
wording encountered while implementing swBroker / swNgsild against NGSI-LD
v1.9.1. Intended as input for ETSI ISG CIM / TC DATA clarifications.

Each entry: **§ ref** · **what we hit** · **what the spec says (or
doesn't)** · **what we did** · **what we'd want fixed**.

---

## Index (by theme, roughly ordered by interop impact)

### A. Distributed operations — concurrent fan-out & policy

- **[27](#27)** § 4.3.6 — concurrent vs sequential forwarding to matching CSRs is unspecified (no permission, no obligation, no constraint)
- **[28](#28)** § 5.2.34 — per-CSR `timeoutMs` semantics when N CSRs forward concurrently
- **[29](#29)** § 4.3.6.5 — outbound header policy on forwarded requests is half-defined (Via, NGSILD-Tenant, Content-Type, Accept, Link, Authorization, X-Forwarded-For, inbound user headers — no normative table)
- **[36](#36)** § 5.2.36 — distop counter (timesSent/Failed/Success/Failure) atomicity under concurrent fan-out and HA replicas sharing a CSR store
- **[31](#31)** § 5.14.4.4 — multi-CSR EntityMap aggregation (linkedMaps shape, dedup rule, multi-source array semantics)
- **[35](#35)** § 5.7.1.4 — auxiliary mode merge when two aux CSRs overlap concurrently (first-wins? timestamp? reject at creation?)
- **[46](#46)** § 6.3.17 — "redirect" mode classified as BOTH single-source and multi-source in the same section (contradiction)
- **[47](#47)** § 6.3.17 — `NGSILD-Warning` header emission semantics are underspecified (no broker emits them today; 4 codes share triggers)

### B. Distributed operations — composition & routing

- **[1](#1)** § 5.6.1.4 — createEntity partial-success body format (no shape defined; same gap for append / update / merge / delete)
- **[30](#30)** § 5.7.2.4 — split-mode forwarded query: which URL params get stripped vs survive vs narrow per CSR (no normative list)
- **[34](#34)** § 5.6.17 + § 4.5.5.3 — Merge Entity composition order when applying across local + N CSRs in parallel
- **[39](#39)** § 5.6 (generic) — collapsing uniform-error multi-CSR responses (207 noise vs single ProblemDetails)
- **[37](#37)** § 5.6.3.4 — `?options=noOverwrite` semantics when the same attr exists locally AND on an exclusive CSR
- **[38](#38)** § 5.6.5 — `?deleteAll=true` forwarded to a CSR that doesn't support `deleteAttrs` (refuse all? skip CSR? partial?)
- **[2](#2)** § 5.6.1.4 + § 4.3.6.3 — exclusive CSR without `createEntity` op: deadlock semantics for fully-claimed input
- **[3](#3)** § 5.6.1.4 — entity shell when all attributes are claimed externally (create `{id, type}` locally or not?)
- **[12](#12)** § 5.7.11 — federation has no hop / TTL bound; loop detection is the only stop
- **[51](#51)** § 5.5.9 / § 6.3.10 — `limit` / `offset` semantics under distributed federation (per-CSR vs global; offset composition)

### C. Response shapes & error formats

- **[32](#32)** § 5.2.6 — ProblemDetails extension fields for NGSI-LD-specific context (`entityId`, `entityIds`, `attributeName`, `registrationId`, `status`)
- **[23](#23)** § 6.3.10 — 206 Partial Content vs 200 OK on temporal queries; ETSI tests are mutually inconsistent
- **[19](#19)** § 5.5.4 / § 5.5.5 — InvalidRequest vs BadRequestData for invalid URL-param values
- **[20](#20)** § 5.8.6 — `attributeDeleted` notification representation: bare-string default vs ETSI-fixture extended-form
- **[50](#50)** § 6.3.7 / § 4.5.16 — GeoJSON negotiation: Accept header vs `?format=geojson` URL param (and the geometryProperty axis)

### D. CSR data model

- **[4](#4)** § 5.2.9 — `endpoint` URL convention: host+port (example) vs full API base (silent normative)
- **[5](#5)** § 5.7.5 + § 5.2.9 — tenant-scoped loop detection alias (per-tenant or per-broker?)
- **[7](#7)** § 5.2.9 — CSR is over-nested and hard to write correctly (frequent user mistake → 422 / vendor-specific salvage)
- **[8](#8)** § 5.2.9 — typos in top-level CSR fields silently accepted as CSF Properties
- **[9](#9)** § 4.3.6.5 / 4.3.6.6 — contextSourceInfo edge cases (well-known keys, banned keys, `urn:ngsi-ld:request` substitution)
- **[10](#10)** § 5.2.9 — location / observationSpace / operationSpace match semantics (containment? overlap?)
- **[45](#45)** § 5.2.9 — CSR `location` shape: bare GeoJSON Geometry (spec) vs wrapped GeoProperty (ETSI fixture)

### E. Discovery & federation

- **[6](#6)** § 5.10.2 — Discovery as a separate registry process (vs the implicit broker-driven matching)
- **[26](#26)** § 5.10.2.5 — RegistrationInfo filtering on GET /csourceRegistrations is "should", not "shall"; per-request opt-in lacking
- **[11](#11)** § 5.7.11 / § 6.25–6.28 — no URL param for "local + registry, no forward" use case

### F. Local operations & data semantics

- **[33](#33)** § 5.6.18 — Replace Entity: can the type change locally? Spec is permissive, implications unspecified
- **[13](#13)** § 5.6 — no Retrieve Attribute operation (forces clients to do GET /entity + project)
- **[14](#14)** § 6.34.3.1 — GET /entityMaps creates (HTTP method semantics violate REST norms)
- **[22](#22)** § 5.6.5 — `?deleteAll=` boolean parsing case-sensitivity
- **[18](#18)** § 5.16.1.4 — snapshot capture from CSRs in no-split deployments
- **[44](#44)** § 5.6.3.4 — `?observedAt=<time>` default-observedAt URL param exists for Merge but not Append/Update

### G. Notifications & subscriptions

- **[21](#21)** § 5.2.12 — entityDeleted vs attributeDeleted trigger symmetry on entity delete
- **[16](#16)** § 4.3.6.6 / § 5.8 — `jsonldContext` fallback asymmetry between subscriptions and CSRs
- **[17](#17)** § 6.3.8 / § 6.3.9 — `urn:ngsi-ld:request` substitution for receiverInfo

### H. JSON-LD / @context

- **[15](#15)** § 6.3.5 — @context placement for array bodies is ambiguous (per-root vs per-element)
- **[43](#43)** § 4.5.24 — JsonProperty inner-value @context interaction: where does opaqueness start and end?

### I. Data model — multi-instance, linked entities, intake order

- **[40](#40)** § 4.6.6 — chronological order on batch arrays is an unenforceable assumption (no mechanism to assert or signal intent)
- **[41](#41)** § 4.5.5 — multi-instance with identical datasetId on intake (reject? dedupe? defer?)
- **[42](#42)** § 4.5.23 — `joinLevel` semantics with cycles, diamonds, and originating-id self-reference
- **[48](#48)** § 4.5.5.3 — "indeterminate / random" tiebreaker on identical datasetIds is unfit for cross-broker interop
- **[49](#49)** § 4.5.5.1 — `datasetId: "@none"` round-trip drops the field on response (POST body not reproducible from GET)

---

<a name="1"></a>
## 1. § 5.6.1.4 — createEntity partial success body format

**Hit:** When some forwards succeed and others fail, the spec says the
response is "a partial success". No body shape defined.

**Spec:** Only phrase is "a partial success if some parts of it succeeded".
§ 5.2.18 defines `UpdateResult` with `updated[]` / `notUpdated[]` for
attribute-update ops, but no equivalent for createEntity.

**Our call:** 207 Multi-Status with body
`{ "notCreated": [ NotUpdatedDetails ] }`, reusing § 5.2.19 shape
(`attributeName`, `reason`, `registrationId?`). Success side implicit.

**Fix wanted:** Spec should define the partial-success body formally —
either extend `UpdateResult` semantics to createEntity, or introduce a
dedicated `CreateResult` type. Same gap appears for append/update/merge/
delete entity ops (§ 5.6.2–5.6.6).

---

<a name="2"></a>
## 2. § 5.6.1.4 + § 4.3.6.3 — exclusive CSR without createEntity op

**Hit:** A CSR with `mode: exclusive` and `operations: ["retrieveEntity"]`
(i.e. no `createEntity`) claims a set of Attributes on an Entity, but:

- Per § 4.3.6.3, the broker cannot hold those Attributes locally (the
  exclusive invariant).
- The forwarded createEntity is refused by the registration.

If the admin creates such a reg, can those Attributes ever be created?

**Spec:** § 5.6.1.4 says "for matching Registrations where the Create
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
## 3. § 5.6.1.4 — entity shell when all Attributes are claimed externally

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
## 4. § 5.2.9 — CSR endpoint URL convention

**Hit:** Is `endpoint` the Context Source's base URL (host+port) with
the broker adding `/ngsi-ld/v1/...`, or is it the full API base path
(host+port+`/ngsi-ld/v1`)?

**Spec:** § 5.2.9 defines `endpoint` as "URI dereferenceable to access
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
## 5. § 5.7.5 + § 5.2.9 — tenant-scoped loop detection alias

**Hit:** Multi-tenant broker. The `Via` header alias for loop detection
must distinguish between the same broker serving different tenants,
otherwise a legitimate tenant-rewrite forward (CSR with `tenant: T2`,
endpoint back to the same broker) is falsely detected as a loop.

**Spec:** § 5.7.5 defines `contextSourceAlias` for loop detection in
general. § 5.2.9 defines `tenant` on a CSR to rewrite the target
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
## 6. § 5.10.2 Discovery — separate registry process

**Hit:** CSR Discovery (`GET /csourceRegistrations?...`) appears to be a
separate concern from a broker's own internal registration cache. The
filter language (§ 5.10.2) is richer than what a broker uses for dispatch.

**Spec:** § 5.10 frames Discovery as a service a Context Registry
offers, but it's unclear whether a Context Broker must also implement
Discovery over its own regCache, or only a Context Registry does.

**Our call:** Broker returns 501 Not Implemented on the rich
Discovery query syntax; offers CRUD only over its own local regCache.

**Fix wanted:** Clarify the role separation: what must a Context Broker
implement vs a Context Registry? Can a broker advertise itself as "no
Discovery support" without breaking federation?

---

<a name="7"></a>
## 7. § 5.2.9 — CSR is over-nested and hard to write correctly

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

**Spec:** § 5.2.10 RegistrationInfo + § 5.2.8 EntityInfo are defined
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
## 8. § 5.2.9 — typos in top-level CSR fields are silently accepted as CSF Properties

**Hit:** A CSR has well-known top-level keys (`id`, `type`, `endpoint`,
`mode`, `information`, `operations`, ...). Anything else at the top
level is treated as a Context Source Property (CSF property —
user-defined metadata about the source).

This means a typo like `"ifnormation"` instead of `"information"` is
**silently accepted**: the typo'd key is stored as a CSF property, and
then validation fails because the mandatory `information` field is
missing. The error message points at the missing mandatory field, not
the typo — debugging is unnecessarily painful.

**Spec:** § 5.2.9 table lists all standard fields; additional fields
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
## 9. § 4.3.6.5 / 4.3.6.6 — contextSourceInfo edge cases

**Hit:** Several under-specified edge cases around contextSourceInfo:

(a) **Banned keys at registration-creation time.** § 4.3.6.5 says binding-
    level headers (e.g. `Content-Length`) "shall be ignored" at forward
    time. But the spec doesn't say whether a CSR with those keys should
    be **rejected at creation** (400 Bad Request) or **silently accepted
    and filtered later**. Same question for `NGSILD-Tenant` — the spec
    says it "shall not be part of contextSourceInfo", with no explicit
    rejection semantics.

(b) **Conflict between `contentType` CSI key and incoming Content-Type.**
    If the CSR declares `contentType: application/json` but the incoming
    request is `application/ld+json` (with payload containing `@context`),
    does the broker strip `@context` before forwarding? § 4.3.6.6 only
    spells this out for the `jsonldContext` case.

(c) **`accept` on write ops.** § 4.3.6.6 says `accept` applies to
    "response from the distributed endpoint". For createEntity a 201
    response is empty-body — does `accept` still get sent as a header?
    The spec doesn't differentiate write vs read ops.

(d) **`urn:ngsi-ld:request` sentinel scope.** § 4.3.6.5 says a value of
    `urn:ngsi-ld:request` means "pull from triggering request", but only
    *some* headers can be meaningfully pulled (e.g. `Authorization`,
    not `Content-Length`). The spec doesn't list which headers are
    eligible.

(e) **Advanced semantics deferred.** `jsonldContext` (§ 4.3.6.6) requires
    the broker to perform JSON-LD compaction against a remote `@context`,
    switch `Content-Type` to `application/json`, and strip `@context`
    from the body. `ngsildConformance` requires a version-compat
    transform (defined in § 4.3.6.8). Both are non-trivial.

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
## 10. § 5.2.9 location / observationSpace / operationSpace — match semantics

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
## 11. § 5.7.11 / § 6.25–6.28 — no URL param for "local + registry, no forward"

**Hit:** Discovery endpoints (/types, /types/{type}, /attributes,
/attributes/{attrId}) have three natural modes per § 5.7.11:

1. **Local only** — only the local datastore.
2. **Local + CSR metadata** — augment with types/attrs declared in
   `information.entities[].type`, `propertyNames`, `relationshipNames`.
3. **Local + CSR metadata + forwarded retrieval** — forward the GET
   to every CSR that supports the relevant retrieve op and merge.

The spec defines `?local=true` (mode 1) and text for § 5.7.11 lays
out both "local + registry" and "forward" as alternatives (mode 2 and
mode 3), but the HTTP binding has **no URL parameter** to ask for
mode 2 specifically. The default is underspecified — implementations
will diverge on whether the default is mode 2 or mode 3.

**Spec:** § 5.7.11 — "implementations may only take into account
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
## 12. § 5.7.11 federation depth — no hop / TTL bound

**Hit:** Mode-3 discovery forwarding is recursive by nature: every
forwarded GET lands on a broker that itself re-forwards to its own
CSRs. In a misconfigured or deep federation this can explode. Via-
header loop detection catches **cycles** but not deep **chains**,
and for discovery (which asks each CSR to aggregate upstream) it
does not stop on legitimate non-cyclic trees that are simply too
deep.

**Spec:** Silent on depth. § 5.7.11 describes forwarding as an option
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
## 13. § 5.6 — no Retrieve Attribute operation

**Hit:** The spec has PATCH/DELETE/PUT on `/entities/{id}/attrs/{attrId}`
but no GET. Retrieving one specific attribute requires fetching the whole
entity and filtering client-side (or using `?pick=`, which still returns
the wrapping entity and its id/type).

**Spec:** § 5.6 covers Partial Attribute Update (5.6.4), Delete Attribute
(5.6.5), Replace Attribute (5.6.19). There is no Retrieve Attribute
operation, and no corresponding CSR op name.

**Our call:** Implemented `GET /entities/{id}/attrs/{attrId}` locally,
returning the Attribute in NGSI-LD API format (plain Property /
Relationship / ... object, no entity wrapping). `?datasetId` filters
the default or specific instance. `?sysAttrs` preserved. DistOps
forwarding not wired — no agreed op name.

**Fix wanted:** Add a Retrieve Attribute operation to § 5.6 with a
defined response shape (plain Attribute vs. minimal entity wrapper)
and a CSR op name (candidate: `retrieveAttribute`) so distributed
retrieval of a single attribute is specified the same way as
update / delete / replace are.

---

<a name="14"></a>
## 14. § 6.34.3.1 — GET /entityMaps creates (HTTP method semantics)

**Hit:** § 6.34.3.1 binds the HTTP GET method on `/entityMaps` to the
"Create EntityMap for Query Entities" operation, responds 201 Created,
and includes an `NGSILD-EntityMap` location header. The same semantics
apply to GET /temporal/entityMaps (§ 6.35.3.1).

**Spec:** GET is defined this way; POST /entityMaps (§ 6.34.3.2) exists
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
## 16. § 4.3.6.6 / § 5.8 — `jsonldContext` fallback asymmetry: subs vs. CSRs

**Hit:** When `jsonldContext` is not specified on a CSR's
`contextSourceInfo`, forwarded requests go out with the original
request's @context handling (no compaction, no context substitution).
Subscriptions get different treatment for the same absent-field case.

**Spec:**

- For **Subscriptions** (§ 5.8.1.4 on create, analogous language on
  update):
  > "The @context to be used for sending Notifications ... shall be the
  > one specified in the jsonldContext field. If not present, the
  > jsonldContext field **shall be initialized** with the @context
  > applicable for the Subscription."

  Net: absent `jsonldContext` → broker auto-fills from the
  subscription-creation request's @context.

- For **CSRs** (§ 4.3.6.6 + § 6.3.19): no analogous fallback clause.
  § 4.3.6.6 only describes what happens when `jsonldContext` IS
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
## 15. § 6.3.5 — @context placement for array bodies is ambiguous

**Hit:** Batch ops (§ 5.6.7 / 5.6.8 / 5.6.9 / 5.6.10 / 5.6.20) and ld+json
listings take/produce a JSON **array** as the payload body. JSON arrays
have no keys, so `@context` cannot appear "at the root of the body".

**Spec:** § 6.3.5 says, for Content-Type `application/ld+json`, "the
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

**Fix wanted:** § 6.3.5 should add one sentence: "For array request
bodies with Content-Type `application/ld+json`, each element in the
array shall carry its own `@context`." Same clarification applies to
response bodies rendered as ld+json arrays.

---

<a name="17"></a>
## 17. § 6.3.8 / § 6.3.9 — `urn:ngsi-ld:request` substitution for receiverInfo

**Hit:** § 4.3.6.5 mandates that a CSR's `contextSourceInfo` value of
`"urn:ngsi-ld:request"` be replaced by the same-named header from the
*triggering* request. § 6.3.18 spells the HTTP-binding rule out. The
receiverInfo paragraphs (§ 6.3.8 entity notifications, § 6.3.9 csource
notifications) describe an identically-shaped KeyValuePair[] but are
silent on `"urn:ngsi-ld:request"`. Reading strictly, the value must be
forwarded verbatim — defeating the same auth-forwarding use case the
contextSourceInfo wording was added for.

**Spec:** silent — § 6.3.8/§ 6.3.9 only say "set equal to the content of
the corresponding 'value' member of the KeyValuePair".

**Our call:** symmetrically apply the substitution to receiverInfo on
the event-driven and CSR-side notify paths. Periodic-notification path
(§ 5.8 / § 5.2.14.1) has no triggering request so a placeholder is
silently dropped there.

**Fix wanted:** § 6.3.8 / § 6.3.9 should reuse the § 6.3.18 sentence:
"unless the special value `urn:ngsi-ld:request` has been set, in which
case the value is to be taken from the triggering request, if present
there." The data-model section (§ 5.2.15 receiverInfo) should also
mention the binding-specific substitution like § 4.3.6.5 does for
contextSourceInfo.

---

<a name="19"></a>
## 19. § 5.5.4 / § 5.5.5 + § 6.3.2 — InvalidRequest vs. BadRequestData for invalid URL-param values

**Hit:** A `GET /csourceSubscriptions?limit=-5&page=2` (or any URL with a
syntactically OK but semantically invalid pagination/value query param)
must be rejected. Both `InvalidRequest` and `BadRequestData` are
defensible per the wording in § 5.5; the ETSI test suite consistently
expects `BadRequestData` (e.g. 041_04_01/02/03, 039_05_01).

**Spec:** Two different sections give two different rules:

- § 5.5.4 (BadRequestData): "The request includes input data which does
  not meet the requirements of the operation." — URL-param values are
  arguably "input data".
- § 5.5.5 (InvalidRequest): "The request associated to the operation is
  syntactically invalid or includes wrong content."
- § 6.3.2: "If an HTTP request for an operation contains parameters that
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
Switching to BadRequestData per § 5.5.4 + the test suite's expectations.

**Fix wanted:** § 6.3.2 / § 5.5.4 / § 5.5.5 should make the split
explicit:

- BadRequestData: invalid value for any URL param defined by NGSI-LD
  (limit, offset, q, attrs, georel, …) — i.e. "data wrong".
- InvalidRequest: unrecognized URL param OR an `options=` value that
  the implementation doesn't support — i.e. "request doesn't make
  sense".

Right now § 5.5.4 wording overlaps with § 5.5.5 wording closely enough
that two implementations can both quote the spec and still disagree.

---

<a name="18"></a>
## 18. § 5.16.1.4 — snapshot capture from CSRs in no-split deployments

**Hit:** § 5.16.1.4 says snapshot queries are executed "following the
behaviour described in clause 5.7.2.4" (the distributed-query path).
§ 5.7.2.4 in turn covers two regimes:
1. *split entities*: each Entity may be sharded across multiple CSRs;
   the broker must forward queries unfiltered, merge per-Attribute (§
   4.5.5.3), and re-apply filters post-assembly (§ 5.5.9.3).
2. *no-split*: every Entity is fully held by one source — used by the
   `splitEntities=false` URL param (§ 5.2.23) and indirectly by some
   broker-wide configuration switches.

For *snapshot creation* in particular, the no-split regime is enormously
faster because:
- the query can be forwarded WITH filters to each CSR,
- responses are simple inserts into the snapshot's storage (no merge),
- no post-merge filter scan over the captured set is needed.

**Spec:** § 5.16.1.4 doesn't acknowledge the split/no-split distinction
at all, just defers to § 5.7.2.4. Nothing forbids a broker from honouring
`splitEntities=false` on snapshot creation, but it's also not made
explicit. § 5.2.41 (Snapshot data type) doesn't list `splitEntities` as
a member of the Snapshot doc, and § 6.3.22 (NGSILD-Snapshot binding)
defines no URL params for the create endpoint.

**Our call:** allow `?splitEntities=true|false` on `POST /snapshots`,
parsed into the same `swNgsild.splitEntitiesSet/Val` used by `GET
/entities`. Default is the broker's `--noSplitEntities` setting. In
no-split mode capture forwards filters to CSRs, dedupes by id when
streaming into the snap-tenant ("first writer wins"), and skips the
post-merge filter scan. Split-mode is a follow-up.

**Fix wanted:**
- § 5.16.1 should say explicitly that snapshot creation MAY accept
  `splitEntities` per § 5.2.23, with the same semantics as on `GET
  /entities`.
- § 6.3.22 should list URL params accepted on `POST /snapshots`, or
  point to § 5.7.2 / § 5.2.23 for the allowed set (so brokers don't
  diverge on which params apply where).
- § 5.16.1.4 step "execute the Queries … following clause 5.7.2.4"
  should make the split / no-split branches of 5.7.2.4 explicit, since
  the implementations are very different on capture cost (TB-scale
  snapshots are practical only in no-split mode without Phase-2c
  per-attribute-merge work).

---

<a name="20"></a>
## 20. § 5.8.6 — attributeDeleted notification representation: bare-string default vs. ETSI fixtures

**Hit:** ETSI test cluster 046_22_* (attributeDeleted notifications) ships
13 expectation fixtures under `data/subscriptions/expectations/` that
unconditionally expect the **object form**:

```json
"locatedAt": { "type": "Relationship", "object": "urn:ngsi-ld:null" }
```

The tested subscriptions set neither `sysAttrs`, `showChanges`, nor a
multi-instance `datasetId`.

**Spec:** § 5.8.6, third bullet under "If a Subscription does not define a
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
- ETSI clarifies that § 5.8.6 always requires the object form regardless
  of sysAttrs/showChanges/datasetId (then the spec text needs amending —
  the "shall" + "unless more information is required" wording today
  unambiguously mandates bare string as the minimum).

The `since_v1.6.1` tag on the affected tests suggests these fixtures may
predate a § 5.8.6 wording change; the framework's
`SystemGeneratedTemporalPropertyOperator` already includes a `deletedAt`
hook, hinting at half-finished alignment.

---

<a name="21"></a>
## 21. § 5.2.12 — entityDeleted/attributeDeleted trigger symmetry on entity delete

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

**Spec:** § 5.2.12 enumerates six triggers — `entityCreated`,
`entityUpdated`, `entityDeleted`, `attributeCreated`, `attributeUpdated`,
`attributeDeleted`. **Only** `entityUpdated` is explicitly stated to
subsume sub-events:

> "entityUpdated" is equivalent to the combination "attributeCreated",
> "attributeUpdated" and "attributeDeleted".

There is **no equivalent statement** for `entityDeleted` ⇒
`attributeDeleted` (or `entityCreated` ⇒ `attributeCreated`, though we
already do that side too).

§ 5.8.6 then says, for the body, "If the notification was triggered by
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
  to § 5.2.12, **and** update § 5.8.6's "only the deletedAt" rule to
  describe the watched-attribute body case explicitly, **or**
- Update the fixtures to use `notificationTrigger: ["entityDeleted"]` and
  drop the per-attribute body content (just `{id, type, deletedAt}`).

Either resolution would let us flip the deviation back to spec-strict
behaviour. Until then, our broker matches what `046_22_12/13` demand.

---

<a name="22"></a>
## 22. § 5.6.5 — `?deleteAll=` boolean parsing case-sensitivity

**Hit:** Python's `requests` library serialises a Python `True` to the
literal string `"True"` (capital T) on URL query params. ETSI test
046_22_08 sends `?deleteAll=True`. The broker was strict-`strcmp("true")`
and silently treated it as false (deleted only `@none` instead of all
instances), causing the wrong notification.

**Spec:** § 6.3.2 / § 5.6.5 don't pin down the case for boolean URL
parameters. The "Boolean" datatype reference (§ 5.2.16) likewise leaves
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
## 23. § 6.3.10 — 206 Partial Content vs 200 OK on temporal queries: ETSI tests are mutually inconsistent

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

**Spec:** § 6.3.10 says "implementations *shall* use the Partial
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
## 26. § 5.10.2.5 — RegistrationInfo filtering on GET /csourceRegistrations is "should", not "shall"; client should be able to pick

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

**Fix wanted:** add a URL param to § 5.10.2.4 (e.g.
`?information=relevant|full`, default `relevant`) so the client picks
the shape per request. That removes the ambiguity ("should") and
keeps both behaviours accessible from one broker without server
restart or vendor-specific switches.

---

<a name="27"></a>
## 27. § 4.3.6 — concurrent vs sequential forwarding to matching CSRs

**Hit:** When a service routine matches N CSRs (across exclusive,
redirect, inclusive, auxiliary), the natural implementation is a
sequential loop `for csr in csrs: forward(csr)`. With N=5 and a
non-responsive CSR endpoint at the 5-second default, that's 25 s of
wall-clock for a request that should be ~5 s.

The fix is to fan out concurrently (curl_multi_perform or equivalent),
but the spec gives no guidance — neither permission nor obligation.

**Spec:** § 4.3.6.1 talks about avoiding overquery but says nothing
about ordering or concurrency of the forwards. § 5.7.1.4 / § 5.7.2.4
describe the processing flow as if it were sequential ("first
exclusive, then redirect, then inclusive"), but those orderings are
about *result composition*, not *request issuance*.

**Our call:** issue all matched forwards concurrently in a single
batch; walk the responses in the sequential order the spec describes
for result composition. Semantically identical to serial dispatch.

**Fix wanted:** an explicit "the Context Broker MAY issue forwarded
requests to matching Context Sources concurrently; the result
composition order defined in § 5.7.1.4 / § 5.7.2.4 / § 4.3.6.3 is
unaffected." Otherwise an interop reviewer can argue "the spec
implies sequential."

---

<a name="28"></a>
## 28. § 5.2.34 — `timeoutMs` semantics under concurrent multi-CSR fan-out

**Hit:** Each CSR may set its own `timeoutMs`. When the broker fans
out to N CSRs concurrently, the natural single-deadline backend
(curl multi, epoll) wants one overall budget, not N independent
deadlines. The spec defines a per-CSR value but is silent on how
that composes when multiple CSRs are active simultaneously.

**Spec:** § 5.2.34 defines `timeoutMs` on `CSourceRegistration` as
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
## 29. § 4.3.6.5 — outbound header policy on forwarded requests

**Hit:** The broker forwards a request to a CSR. Which headers
*must* it add, which *may* it add, which *must* it copy verbatim
from the inbound request? Today's spec text addresses only a subset
(contextSourceInfo, banned keys, jsonldContext → Link). The
mandatory baseline is not enumerated.

**Spec:** § 4.3.6.5 enumerates banned `contextSourceInfo` keys
(Content-Length, Host, NGSILD-Tenant, ngsildConformance). § 5.7.5
defines `Via` for loop detection. § 4.7 / annexes touch
NGSILD-Tenant on the wire. No single section says "every forwarded
request shall carry headers X, Y, Z" or "the inbound headers A, B, C
shall be propagated".

**Our call:**

| header           | policy                                                  |
|------------------|---------------------------------------------------------|
| Via              | append own-alias; preserve inbound Via chain            |
| NGSILD-Tenant    | set to csr.tenant (§ 5.2.9 rewrite)                     |
| Content-Type     | default `application/ld+json`; csi.contentType override |
| Accept           | set from csi.accept (no default)                        |
| Link             | from csi.jsonldContext                                  |
| Authorization    | dropped (not propagated)                                |
| X-Forwarded-For  | not added (we don't track this today)                   |
| inbound user hdrs| not propagated unless named in csi                      |

**Fix wanted:** § 4.3.6.5 should have a table identical to the
above (or its agreed equivalent). Without it, two compliant brokers
can disagree on outbound shape, with downstream CSRs seeing
different headers depending on broker vendor.

---

<a name="30"></a>
## 30. § 5.7.2.4 — split-mode forwarded query: which URL params survive?

**Hit:** In "split entities" mode, a single entity's attributes may
live on multiple CSRs. Forwarding the user's `q`, `geoQ`, or `scopeQ`
to one CSR is unsafe because that CSR sees only part of the entity
and may falsely exclude it. The broker must therefore strip those
filters from the forwarded URL — but the spec doesn't enumerate
*which* filters are unsafe.

**Spec:** § 5.7.2.4 mentions split-entities behaviour in passing
("if the broker holds part of an Entity locally"), without giving
the list of forwardable params.

**Our call:** in split mode, forward only `id`, `idPattern`, `type`
(guaranteed-on-every-fragment per § 4.5.5 / § 5.2.6). Strip
`q`, `geoQ`, `scopeQ`, `attrs`, `format`, `local`, `entityMap`,
`orderBy`, `collation`. `pick` MAY be narrowed per CSR (intersect
with the CSR's registered exports).

**Fix wanted:** § 5.7.2.4 should contain an explicit table:
"In split-mode federation, the following URL params SHALL be
stripped from the forwarded request: …; the following params MAY be
narrowed per CSR: …; the following are forwarded as-is: …".
Without that table, interop on the same query across vendors is a
coin toss.

---

<a name="31"></a>
## 31. § 5.14.4.4 — multi-CSR EntityMap aggregation

**Hit:** GET /entityMaps under federation: each matched CSR returns
its own EntityMap. The broker must build a single EntityMap response
that captures *which entity came from which CSR(s)*. The spec
defines the single-source EntityMap shape but not the aggregation.

**Spec:** § 5.14.4.4 defines `entityMap` as `{ <entityId>: [src1,
src2, …] }`. § 5.2.42 mentions `linkedMaps` for cross-broker
references. Nothing ties them together for the aggregation flow.

**Our call:** broker's response includes:
- `entityMap`: union over all CSRs' entries, deduped by entityId,
  source array merged.
- `linkedMaps`: `{ <csr.regId>: <remote-map-id> }` so the client
  can re-page the remote map directly. The local map's source
  array for an entity is the set of `regId`s that returned it.

**Fix wanted:** § 5.14.4.4 should formalise the aggregation:
linkedMaps shape, dedup rule, and the source-array semantics when
the same entity appears on multiple CSRs (we use union; some might
argue "first wins").

---

<a name="32"></a>
## 32. § 5.2.6 — ProblemDetails extension fields for NGSI-LD errors

**Hit:** Every error response in our broker that names a specific
entity wants to surface that entity's id. BatchEntityError already
carries it; plain ProblemDetails (the 400/404/409 family) doesn't.
Today the entityId is buried in the `detail` string, which is
human-prose only.

**Spec:** § 5.2.18 defines BatchOperationResult / BatchEntityError
with `entityId`. § 5.2.6 defines ProblemDetails per RFC 7807 with
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

**Fix wanted:** § 5.2.6 / Annex B should bless a small set of
NGSI-LD-specific ProblemDetails extension keys. The current
"undefined" position guarantees vendor lock-in.

---

<a name="33"></a>
## 33. § 5.6.18 — Replace Entity: can the type change?

**Hit:** PUT /entities/{id} with `type: B` against a stored entity
of `type: A`. The spec wants "complete replacement". Allowing the
type to change has cascading implications: subscriptions filtered
by type, TRoE indices, type-scoped CSR matches, etc.

**Spec:** § 5.6.18.4 — "If the target Entity exists locally,
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
## 34. § 5.6.17 + § 4.5.5.3 — Merge Entity composition order across local + multiple CSRs

**Hit:** PATCH /entities/{id} (Merge) against an entity with attrs
distributed across local + N inclusive CSRs. Each side may apply
the merge differently (different `defaultObservedAt`, instance-level
conflict resolution per § 4.5.5.3). The single response body wants
to be the merged-entity view, but it's a composition of N+1
results.

**Spec:** § 5.6.17 describes single-source merge. § 4.5.5.3
describes per-instance conflict rules within one source. Nothing
defines the order or rule when several sources merge in parallel
and the broker must compose them.

**Our call:** local merge first (the broker's own view), then walk
forwards in mode order (exclusive → redirect → inclusive). Each
remote source's result is re-merged into the running composite via
§ 4.5.5.3 timestamp rules. This is deterministic but vendor-policy.

**Fix wanted:** § 5.6.17 should state the composition order
explicitly, OR explicitly say "the composition order is
implementation-defined; the resulting entity SHALL satisfy
§ 4.5.5.3 against the union of all per-source results". The latter
is weaker but at least removes the "is order observable?" question.

---

<a name="35"></a>
## 35. § 5.7.1.4 — Auxiliary mode result merge under concurrent fan-out

**Hit:** Auxiliary mode "fills gaps only" — its attributes are added
to the composite only where no earlier source provided them. With N
auxiliary CSRs in flight concurrently, two of them may both contribute
attr X. Which one wins?

**Spec:** § 4.3.6.2 / § 5.7.1.4 describe auxiliary as gap-filler
without ordering. "Concurrent two-aux overlap" is unspecified.

**Our call:** walk auxiliary results in CSR-cache-iteration order
(stable but vendor-internal); first-write wins for any attr the
composite is still missing.

**Fix wanted:** § 5.7.1.4 should pick:
(a) the composite is order-independent — two aux CSRs claiming the
same attr is "any of them wins, undefined which", and clients shall
not rely on a specific source, or
(b) the broker SHALL apply § 4.5.5.3 timestamps across aux sources
too (most-recent wins), or
(c) auxiliary CSRs SHALL not overlap — overlap is a configuration
error the broker MAY reject at CSR creation.

Today every broker silently picks one of these.

---

<a name="36"></a>
## 36. § 5.2.36 — distop counter atomicity under concurrent fan-out and HA

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

**Spec:** § 5.2.36 lists the counters as part of CSR state. No
section addresses concurrent update or HA.

**Our call:** atomic `$inc` on every increment, `$max`/`$min` for
lastSuccess/lastFailure timestamps. Updates are best-effort flushed
periodically, not synchronously per forward.

**Fix wanted:** § 5.2.36 should say "Context Brokers SHALL ensure
counter updates are atomic with respect to concurrent forwards
issued by the same broker or by replicas sharing the CSR store.
Updates MAY be deferred (eventual consistency) but SHALL be
monotonic." Otherwise interop is a vendor lottery.

---

<a name="37"></a>
## 37. § 5.6.3.4 — `?options=noOverwrite` semantics when the same attr is on a CSR

**Hit:** POST /entities/{id}/attrs with `?options=noOverwrite=true`.
The local entity already has attr X. An exclusive CSR also claims X.

- Local rule (§ 5.6.3.4): noOverwrite means "skip attrs already
  present".
- Distop rule (§ 4.3.6.3): exclusive CSR owns its claimed attrs,
  local SHALL NOT hold them.

The two collide:
- If the broker forwards X to the CSR, the CSR may itself apply
  noOverwrite based on *its own* state and reject — or not.
- If the broker also chops X from local input, the local
  noOverwrite-skip never triggers because X is gone.
- The response body should report X under `notUpdated` (noOverwrite
  skip) or `updated` (CSR took it) — depending on what the CSR did,
  not the local state.

**Spec:** § 5.6.3.4 describes the local case; § 4.3.6.3 describes
the exclusive case. The interaction is silent.

**Our call:** chop precedes local noOverwrite. The forwarded
request includes `?options=noOverwrite`. Whatever the CSR reports
back drives the per-attribute outcome in `updated/notUpdated`.

**Fix wanted:** § 5.6.3.4 should add: "When `options=noOverwrite`
combines with forwarded operations, the URL param SHALL be
preserved on the forward; the CSR's per-attribute outcome
determines the response classification for that attribute,
overriding any inference the broker might make from local state."

---

<a name="38"></a>
## 38. § 5.6.5 — `?deleteAll=true` forwarded to a CSR that doesn't support `deleteAttrs`

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

**Spec:** § 5.6.5 describes the local delete. § 4.3.6.3 mandates
the exclusive chop. § 5.6.1.4 has analogous language for
createEntity ("Conflict ... or partial success"), but Delete is
silent.

**Our call:** option (c). Record `{ entityId, error: Conflict,
detail: "exclusive registration does not support deleteAttrs",
registrationId }` in errors[], local delete proceeds. Response is
207 if local-success + per-attr conflict, 409 if no local success.

**Fix wanted:** § 5.6.5 should mirror the explicit clause already
in § 5.6.1.4 — naming the partial-success / Conflict outcome
explicitly so vendors don't pick (a), (b), or (c) on a whim.

---

<a name="39"></a>
## 39. § 5.6 (generic) — collapsing uniform-error multi-CSR responses

**Hit:** A distributed write operation matches N CSRs; every single
one returns the same error type (e.g. ResourceNotFound on a non-
existent entity, or Conflict). The natural shape is 207 Multi-Status
with N entries — but a 207 carrying N identical "Not Found" entries
is noise. A single 404 + ProblemDetails is what the client expects.

**Spec:** § 5.2.18 defines BatchOperationResult (207). No section
addresses collapsing.

**Our call:** when every `errors[]` entry shares the same
`error.type`, collapse:
- All ResourceNotFound → 404 + plain ProblemDetails.
- All Conflict → 409 + plain ProblemDetails.
- Otherwise → 207 Multi-Status.

The collapsed body includes `entityId` (single entity) or
`entityIds` (multiple) as ProblemDetails extensions (see entry 32).

**Fix wanted:** § 5.2.18 should bless the collapse rule explicitly,
naming the two ergonomic cases (ResourceNotFound → 404, Conflict →
409) and reserving 207 for genuinely mixed outcomes. Vendors that
return 207 for every multi-CSR response (the strict reading) bury
the actual cause behind a Multi-Status envelope.

---

<a name="40"></a>
## 40. § 4.6.6 — chronological order on batch arrays is an unenforceable assumption

**Hit:** Spec assumes the client orders array elements (in
postEntityBatch* bodies, in temporal POST instance arrays, etc.) by
time-of-arrival. The broker uses array index as time. But nothing in
the body lets the client *assert* an order — and the broker can't
verify one, because the timestamps (createdAt/modifiedAt) are
server-stamped on arrival, not carried by the client.

So the "assumption" is functionally a hope. Two valid clients can
submit the same logical sequence in different array orders and get
divergent observable state.

**Spec:** § 4.6.6 — "The Context Broker may assume that array
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
## 41. § 4.5.5 — multi-instance with identical datasetId on intake

**Hit:** POST /entities or POST /entities/{id}/attrs with an
attribute whose value is an array of N instances — two of which
have the *same* `datasetId`. What should happen?

Options:
- Reject with 400 BadRequestData (the input is internally
  contradictory).
- Silently dedupe — keep the last one, or first one, or run
  § 4.5.5.3 timestamp resolution between them.
- Accept both, but the storage layer subsequently picks one (which?).

**Spec:** § 4.5.5 defines `datasetId` as the unique key within an
attribute. § 4.5.5.3 describes inter-source merge resolution but
*assumes* each source has already deduped its own instances. The
intake case (one client, two instances with same dsKey in one
request) is silent.

**Our call:** dedupe at intake (`ldDatasetIdDedup`) per § 4.5.5.3
timestamp rules: most-recent wins. The client sees only one
instance survive.

**Fix wanted:** § 4.5.5 should say explicitly: "If the input
payload contains multiple instances of the same Attribute with
identical datasetId, the Context Broker SHALL reject with
BadRequestData / SHALL dedupe per § 4.5.5.3 / SHALL accept all and
defer resolution to ...". Three vendors will pick three answers
otherwise.

---

<a name="42"></a>
## 42. § 4.5.23 — `joinLevel` semantics with circular references

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

**Spec:** § 4.5.23 + § 5.7.1.4 introduce `joinLevel`, `join`, and
mention `containedBy` cycle prevention. The interaction matrix
(depth × cycle × diamond × originating-id) isn't laid out.

**Our call:**
- Cycle detection by id only — if a target id is already in the
  containedBy chain for the current path, skip the expansion.
- Diamond: each path expands independently; no global dedup of
  expansions.
- joinLevel applies post-cycle: a cycle-closure counts as a "stop",
  not as a step.

**Fix wanted:** § 4.5.23 should pin down the four-way interaction
in a small worked example (the same example we cite here) so two
brokers given the same graph + same `join=inline&joinLevel=3`
produce the same response.

---

<a name="43"></a>
## 43. § 4.5.24 — JsonProperty inner-value @context interaction

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

**Spec:** § 4.5.24 says JsonProperty's value is opaque. The
expansion boundary is intuitive but not normative.

**Our call:** strict boundary — the `json` value is treated as a
black box. Inner `@context` is data, not directive. The outer
wrapper is normal NGSI-LD.

**Fix wanted:** § 4.5.24 should add: "The Context Broker SHALL NOT
process JSON-LD constructs inside the `json` value, including but
not limited to `@context`, `@id`, `@type`, `@vocab`, prefixed
names. The value SHALL be preserved byte-for-byte (up to
JSON-canonicalisation) between intake and retrieval."

---

<a name="44"></a>
## 44. § 5.6.3.4 — `?observedAt=<time>` default-observedAt URL param

**Hit:** Append Attributes / Update Attributes / Merge Entity all
benefit from a default `observedAt` value that the broker injects
into each attribute instance that doesn't already carry one. Today
we accept `?observedAt=` for Merge (§ 6.5.3.4) and apply it during
ldEntityMerge.

The URL param is mentioned in passing for Merge but absent from the
Append / Update routes — yet the same use case ("I just sampled
these N sensors at time T, attach T to all of them as observedAt")
applies equally to Append.

**Spec:** § 6.5.3.4 (Merge) — `observedAt` URL param defined.
§ 6.5.2.4 (Update), § 5.6.3.4 (Append) — no equivalent.

**Our call:** Merge only, matching the spec literally. Clients
wanting the same on Append/Update must rewrite the payload.

**Fix wanted:** § 5.6.3.4 / § 6.5.2.4 should adopt the same
`?observedAt=` semantics as § 6.5.3.4. Same shape, same
default-injection rule.

---

<a name="45"></a>
## 45. § 5.2.9 — CSR `location` shape: bare GeoJSON Geometry vs full GeoProperty?

**Hit:** A CSR's `location` (and `observationSpace`, `operationSpace`)
field — what's its shape?

- Per § 5.2.9 + § 4.7, it's a GeoJSON Geometry: `{ "type":
  "Polygon", "coordinates": [...] }`. Bare geometry, no NGSI-LD
  wrapper.
- Per the NGSI-LD entity rendering rules, a Property of type
  GeoProperty is `{ "type": "GeoProperty", "value": { GeoJSON } }`.
  Wrapper required.

The CSR's `location` is *not* an entity-level GeoProperty — it's a
cross-domain ontology field, simplified shape only. But ETSI
fixtures and several implementations have submitted CSRs with the
wrapper form, and brokers have had to accept both.

**Spec:** § 4.7 → bare GeoJSON Geometry on CSR location. § 5.2.9
Example C.3 confirms (bare polygon). But the normative text doesn't
contrast against the wrapped form, and at least one ETSI fixture
sends the wrapped form to a CSR.

**Our call:** strict bare-geometry on intake (spec-conformant);
clients sending the wrapper get 400. We previously tried "intake
normalize" (auto-wrap → unwrap) but reverted — see commit
`98efede`.

**Fix wanted:** § 5.2.9 should explicitly say: "The `location`,
`observationSpace`, and `operationSpace` fields of a
ContextSourceRegistration are GeoJSON Geometry objects per § 4.7.
They are NOT NGSI-LD GeoProperty representations; the wrapper
shape `{ "type": "GeoProperty", "value": ... }` SHALL NOT be
accepted." And the ETSI fixture that sends the wrapped form should
be fixed accordingly.

---

<a name="46"></a>
## 46. § 6.3.17 — "redirect" mode classified as BOTH single-source and multi-source (contradiction)

**Hit:** The status-code mapping table in § 6.3.17 lists two
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

**Spec:** literally as quoted above, § 6.3.17.

**Our call:** treat `redirect` as multi-source (the 207 path) —
matches § 4.3.6.3 where redirect explicitly allows multiple co-
existing CSRs. We use 502 generically for transport failure of a
single match.

**Fix wanted:** § 6.3.17 should clearly group:
- `exclusive` → at most one source → 508 / 504 / 404 / 502
- `redirect` → multiple co-existing sources → 207 (single failure)
  / 502 (all fail with mixed) / 504 (all timeout)
- `inclusive` → 207 on any failure
- `auxiliary` → 207 on any failure (or silent, since aux is gap-
  fill)

The current text reads as a copy-paste accident.

---

<a name="47"></a>
## 47. § 6.3.17 — `NGSILD-Warning` header emission semantics

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

**Spec:** § 6.3.17 table 6.3.17-1.

**Our call:** we don't emit any NGSILD-Warning headers today. The
status code + ProblemDetails body carry the same information
sufficiently. ETSI fixtures haven't asserted on these warnings yet.

**Fix wanted:** § 6.3.17 should define for each warning code:
(a) the exact triggering condition (single attribute? cached vs
live response? loop vs timeout?),
(b) the HTTP status code(s) it MAY accompany (200? 502? 207?),
(c) whether multiple warning codes can appear simultaneously on
the same response.

Without that, the warning header is an interop dead-letter — every
broker that wants to claim conformance just doesn't emit it.

---

<a name="48"></a>
## 48. § 4.5.5.3 — "indeterminate / random" tiebreaker is unfit for interop

**Hit:** Conflict resolution for two Attribute instances sharing a
`datasetId`, both lacking `observedAt` and `modifiedAt` (or with
identical values):

> If no other mechanism for determining the most current Attribute
> instance is found, the NGSI-LD system shall choose the Attribute
> instance at random and the result is indeterminate.

"Indeterminate" is acceptable for a single broker; for federation
across brokers it is a guaranteed interop failure — same input,
different output depending on which broker handled the merge.

**Spec:** § 4.5.5.3, verbatim.

**Our call:** deterministic last-wins by array index (which is the
client-supplied ordering per § 4.6.6). Two of our brokers given
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
## 49. § 4.5.5.1 — `datasetId: "@none"` round-trip drops the field

**Hit:** Client sends `{"speed":{"type":"Property","value":42,
"datasetId":"@none"}}`. Per § 4.5.5.1 this is equivalent to
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

**Spec:** § 4.5.5.1 says "@none" is the default instance AND says
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
## 50. § 6.3.7 / § 4.5.16 — GeoJSON representation negotiation: Accept header vs format param

**Hit:** A client wants entities rendered as GeoJSON Feature
objects (§ 4.5.16). Two ways to ask:

- `Accept: application/geo+json`
- `?format=geojson` (or similar URL param)

The spec doesn't pin which one is normative. ETSI fixtures use
Accept; some interop guides reference a URL param. The selection
of geometry property (`?geometryProperty=location` vs another) is
itself a separate axis.

**Spec:** § 4.5.16 defines the GeoJSON representation. § 6.3.15
mentions GeoJSON in passing. § 6.3.7 covers representation
selection but doesn't enumerate GeoJSON specifically.

**Our call:** `Accept: application/geo+json` triggers GeoJSON
rendering. `?format=geojson` is also accepted as an alias.
`?geometryProperty=` selects the geometry source attribute (default
"location"). Same logic on collection (FeatureCollection) and
single-entity (Feature) endpoints.

**Fix wanted:** § 6.3.7 should list the GeoJSON representation
explicitly, name `Accept: application/geo+json` as the canonical
trigger, name the geometryProperty interaction explicitly, and
state whether `?format=geojson` is a normative alias or a vendor
extension.

---

<a name="51"></a>
## 51. § 5.5.9 + § 6.3.10 — `limit` / `offset` semantics under distributed federation

**Hit:** GET /entities?limit=20&offset=40 over an entity space
distributed across 5 CSRs. Is `limit=20` per-CSR (each returns up
to 20, broker dedupes locally → may exceed 20 in the response) or
global (broker stops aggregating at 20 across all sources)? Where
does `offset=40` apply — globally, or first-N from broker view,
then the remaining offset against the federated set?

EntityMap (§ 5.14) addresses the *consistency* of pagination
(snapshot of IDs so subsequent pages return the same entities),
but not the FIRST-page semantics.

**Spec:** § 5.5.9 defines limit/offset on a single broker. § 6.3.10
covers the HTTP binding. § 5.14 covers EntityMap. The composition
of limit/offset across federated sources is not addressed.

**Our call:** first page — request each CSR with the user's
limit/offset verbatim, merge + dedup locally, trim to user's
limit. EntityMap captures the resulting ID set for subsequent
pages.

**Fix wanted:** § 5.5.9 should add a sub-clause: "Under
distributed federation, limit and offset apply to the broker's
merged result set. The Context Broker MAY forward limit and offset
to each source as a hint, but the merged-set bound is normative.
EntityMap (§ 5.14) provides consistent pagination across follow-up
page requests."

---

## Template for new entries

```
## N. § X.Y.Z — one-line summary

**Hit:** what we ran into.

**Spec:** what the spec says (or doesn't).

**Our call:** what we implemented.

**Fix wanted:** what we want clarified.
```
