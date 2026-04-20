# NGSI-LD Spec Doubts & Gaps

Running list of ambiguities, silent cases, and genuinely contradictory
wording encountered while implementing swBroker / swNgsild against NGSI-LD
v1.9.1. Intended as input for ETSI ISG CIM / TC DATA clarifications.

Each entry: **§ ref** · **what we hit** · **what the spec says (or
doesn't)** · **what we did** · **what we'd want fixed**.

---

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

## Template for new entries

```
## N. § X.Y.Z — one-line summary

**Hit:** what we ran into.

**Spec:** what the spec says (or doesn't).

**Our call:** what we implemented.

**Fix wanted:** what we want clarified.
```
