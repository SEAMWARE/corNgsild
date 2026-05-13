# ETSI NGSI-LD Test Suite — Issues & Doubts

Running list of issues encountered in the ETSI NGSI-LD Test Suite (the
official ISG CIM conformance suite at
`https://forge.etsi.org/rep/cim/ngsi-ld-test-suite`) while running it
against swBroker. Some are bugs in the test code, some in the test
framework (`robotframework-httpctrl`), some are environmental
assumptions. Intended as input for upstream issue reports.

Each entry: **what we hit** · **where** · **why it's wrong** · **impact** ·
**workaround / fix wanted**.

---

## 1. `Get Stub Count` int vs Robot literal string

**Hit:** Tests do
```robot
${stub_count}=    Get Stub Count    POST    /...
Should Be Equal   ${stub_count}    1
```
and fail with `1 (integer) != 1 (string)`.

**Where:** `Get Stub Count` is implemented in
`libraries/robotframework-httpctrl/src/HttpCtrl/__init__.py:1162` —
returns a Python `int`. Robot's `${...}` literal parsing makes the
right-hand `1` a string. Robot's `Should Be Equal` is type-strict by
default.

**Why it's wrong:** The test author wanted "stub was hit exactly once".
The expected idiom is `Should Be Equal As Integers ${stub_count} 1` or
`Should Be Equal ${stub_count} ${1}`.

**Impact:** Tests that pattern-match. Confirmed in our partial run on
nine DistOps batch / replace tests:
`D012_01_inc / _red`, `D013_01_inc`, `D014_01_red / _02_red`,
`D015_01_exc / _inc / _red`, `D016_01_inc / _red`, `D007_01_red`.
The broker is doing the right thing — stub IS hit once — but the
assertion fails on type alone.

**Workaround / fix wanted:** Patch each test to use one of the
type-relaxed forms above, OR change `get_stub_count` to return `str`.
Upstream issue: file against `forge.etsi.org/rep/cim/ngsi-ld-test-suite`.

---

## 2. HttpCtrl stub-server doesn't reply to broker forwards

**Hit:** Tests register a stub via `Set Stub Reply`, then expect the
broker to forward to the mock and receive that stubbed reply. The
broker's forward TCP-connects and sends the request, but the mock
either closes the connection or never replies. Broker side sees
`SWC_ERR_CLOSED` after ~5 s, treats CSR as failed, surfaces 404 / [].

**Reproduced isolated:** with a tiny standalone Python `BaseHTTPRequestHandler`
on `0.0.0.0:8086` mimicking what `Set Stub Reply` should do, the broker
forwards correctly, gets 200 + body, and the test scenario passes
end-to-end. So the broker side is provably correct.

**Where:** `libraries/robotframework-httpctrl/src/HttpCtrl/http_handler.py`
`__default_handler` and `http_stub.py` `__is_satisfy`. The stub matcher
has odd URL-prefix semantics (lines 79–141 in `http_stub.py`):
- For GETs with `?` in the URL it does substring matching of stub
  params against criteria params, which can both false-match and
  false-miss depending on param order.
- For requests with `urn` in the URL it does a `for elements in id:`
  scan that is brittle.
- The `if "urn" in stub.criteria.url:` branch never returns False on a
  no-match path that has `?` — it returns False above. The control flow
  isn't easy to reason about.

When `__is_satisfy` returns False for a request that the test author
thought it would match, the handler falls through to
`ResponseStorage().pop()` (line 88-92), which **blocks indefinitely**
waiting for an explicit `Wait For Request` + `Reply By` keyword —
neither of which the test calls. Broker times out and treats the CSR as
unreachable.

**Impact:** Many DistOps tests where the broker-to-CSR URL doesn't
literal-match the stub URL exactly: timeouts (8 tests), `204 != 404`
(7 tests), `200 != 404` (5 tests), `Lengths are different: 1 != 0`
(6 tests). Confirmed by running the same broker against our own
python-`http.server` mock — passes.

**Workaround / fix wanted:** Either
1. Replace the stub matcher with strict literal `(method, url)` tuple
   matching, or
2. Document the matcher's exact semantics and have the tests construct
   stub URLs that match what NGSI-LD brokers produce on the wire.

A minor side-issue: the handler's blocking fallback is unfriendly —
when no stub matches, returning a default 404 immediately would let
broker-side error handling exercise (and would be obvious in test
logs).

---

## 3. Mock server started AFTER CSR registered

**Hit:** Standard test setup is
```robot
${response1}=    Create Context Source Registration With Return  ...
Check Response Status Code  201  ${response1.status_code}
Start Context Source Mock Server     # only AFTER the POST returned
```

**Where:** Most DistOps test `Setup Registration And Start Context Source Mock Server`
keywords (e.g. D003_01_red.robot, D004_01_red.robot, ...).

**Why it's wrong:** The broker probes `<endpoint>/info/sourceIdentity`
at CSR-registration time (§ 5.15 / § 5.2.40). If the mock isn't up yet,
the probe fails. Our broker treats probe failure as benign (CSR still
active, reactive Via-loop detection still works — see
`swNgsild/ldRegCache.c:402-418`), but a stricter implementation could
mark the CSR unreachable and refuse forwards.

**Impact:** None on swBroker thanks to lenient probe handling, but
flaky against any broker that relies on probe success. Wastes ~5 s per
test on the failed probe (configurable; we use a short default).

**Fix wanted:** Either start the mock first, or have a `Wait For
Server Up` keyword in the setup before the CSR POST.

---

## 4. Aggregation fixture timestamps in canonical seconds form

**Hit:** Aggregated-temporal expectation files use `2020-08-01T12:03:00Z`
(no fractional second). Brokers that emit `2020-08-01T12:03:00.000Z`
fail string-equality.

**Where:** Examples
- `data/expectations/temporalEntities/vehicle-temporal-representation-aggregated-avg-PT1H.json`
- The `ObservedAtPropertyOperator` in
  `libraries/assertionUtils.py:58` does relax DateTime comparisons —
  but only for fields whose JSON path ends in `['observedAt']`.
  Aggregation bucket tuples are `[value, t-start, t-end]` — paths like
  `root[0]['speed']['avg'][0][1]` — and don't get the relaxed match.

**Why it's wrong:** ISO 8601 / RFC 3339 both allow optional fractional
seconds. Both forms are spec-conformant. String-strict equality on
DateTime is fragile.

**Impact:** Aggregation tests `020_11_*` and `021_*` series, all temporal
retrieve / query tests that don't go through `observedAt`. We worked
around it by trimming `.000` from broker output when sub-second is zero,
but a broker that always emits ms (which is also valid) would still fail.

**Fix wanted:** Apply DateTime-aware comparison (parse as datetime,
compare instants) to ALL DateTime fields, not just `observedAt`. The
existing operator infrastructure makes that straightforward; matcher
predicate just needs a wider net.

---

## 5. Compound `@context` fetched from forge.etsi.org over the internet

**Hit:** Tests Link-header-reference
`https://forge.etsi.org/rep/cim/ngsi-ld-test-suite/-/raw/develop/resources/jsonld-contexts/ngsi-ld-test-suite-compound.jsonld`
and expect the broker to fetch + cache it.

**Where:** `resources/variables.py:3` defines `ngsild_test_suite_context`
to that absolute forge URL.

**Why it's wrong:** Tests are not hermetic — a broker behind a
restrictive firewall, or run when forge.etsi.org is down, will fail.
The test suite repo *contains* the @context document at
`resources/jsonld-contexts/ngsi-ld-test-suite-compound.jsonld`, but
points the broker at the forge URL.

**Impact:** First-run failures on isolated networks. Cache poisoning if
forge.etsi.org serves a different version than what the test fixture
expects.

**Fix wanted:** Have a local-server step in the suite setup that
serves the in-repo `@context` files at a `localhost:<port>/contexts/`
URL, and rewrite `ngsild_test_suite_context` to point there.

---

## 6. Some tests expect 206 for temporal queries that aren't truncated

**Hit:** `021_15_04 / _05 / _06 / _07 / _08`, `020_05_01 / _02`,
`020_13_01 / _06 / _07 / _08 / _09`, `021_16_01` — all expect 206 when
returning the full Temporal Evolution of an Entity. `021_03_01` (with
`?lastN=4`) expects 200.

**Where:** `TP/NGSI-LD/ContextInformation/Consumption/TemporalEntity/...`

**Why it's wrong:** § 6.3.10 defines 206 + Content-Range as **conditional**
on truncation: "if the implementation is not able to respond with the
full representation at once". A response that fits within the
implementation cap is a normal 200. The ETSI fixtures contradict this
in two directions at once: 021_15 expects 206 for a 20-instance result
(no truncation by any reasonable cap), 021_03 expects 200 for a
`lastN=4` result (which our reading of the spec says SHOULD be 206 — a
client-requested truncation is still a truncation).

**Impact:** Inconsistent — there's no way for an implementation to
match all of these tests simultaneously without divergent logic per URL
shape. Currently 11 fail with `206 != 200` (broker correct, test
expectation drift); 1 fails the other way.

**Fix wanted:** Pick a coherent rule and propagate to all temporal
fixtures. Our reading: 206 whenever any truncation happened, including
client-requested via `?lastN`; 200 otherwise. § 6.3.10 should be amended
to make this unambiguous.

---

## 7. ETSI compound `@context` defines `Vehicle` to a non-default IRI

**Hit:** With Link-header `compound`, `?type=Vehicle` expands to
`https://ngsi-ld-test-suite/context#Vehicle`. CSR registrations that
re-use the same compound `@context` register types under that IRI;
mocks that send entities with bare `"type":"Vehicle"` and no `@context`
get the entity expanded under the broker's request context, NOT under
the mock's implicit default. This caused us a long debug session
because the failure mode (silent post-merge type drop) is invisible
without logs.

**Where:** Implicit assumption across the DistOps test suite.

**Why it's noteworthy:** If the broker were strict — "a CSR response
without @context defaults to core, and core-Vehicle is a different IRI
than compound-Vehicle, so don't merge" — every DistOps test would fail.
swBroker now handles this by expanding the CSR response in the user's
context (commits `95fc80d` swJsonld + `676b267` sw broker on the
sw stack; `246836e` / `56f2051` on fw). But the spec is ambiguous about
which @context wins for an unannotated CSR response.

**Fix wanted:** § 4.5 should pick a side. Either "CSR response without
@context inherits the request's @context" (what we and ETSI do
implicitly) or "always defaults to core". One of the two needs to be
explicit in the spec.

---

## 8. CSR registration in test fixtures has `"endpoint": "http://my.csource.org:1026"`

**Hit:** The literal CSR fixture files (e.g.
`data/csourceRegistrations/context-source-registration-vehicle-complete.jsonld`)
hardcode `endpoint: http://my.csource.org:1026`, a non-resolving
hostname.

**Where:** Most CSR fixtures.

**Why it's noteworthy:** Tests rely on
`Prepare Context Source Registration From File` (in
`resources/ApiUtils/ContextSourceRegistration.resource`) to rewrite the
endpoint to `http://${context_source_host}:${context_source_port}`
(=0.0.0.0:8086 by default) before posting. Anyone who copies a fixture
and forgets the rewrite step gets unhelpful failures.

**Fix wanted:** Either drop the bogus URL from fixtures (use
`__REPLACE__` or the variable form directly), or document the rewrite
step prominently.

---

## 9. `020_17 / 020_18 / 020_19 / 020_20` (and similar) `Test Setup` is incomplete — POST `/temporal/entities` is not enough to make `DELETE /entities/{id}/attrs/{name}` succeed

**Hit:** `020_17_01..03`, `020_18_01..03`, `020_19_01`, `020_20_01`,
`020_15_01/02`, `020_12_02`, `021_09_01/02` — every test in the
"deleted attribute leaves a `deletedAt` tombstone" cluster.

**Where:** `TP/NGSI-LD/ContextInformation/Consumption/TemporalEntity/RetrieveTemporalEvolutionOfEntity/020_{15,17,18,19,20}*.robot`
(plus 020_12_02 in the same family, plus 021_09 on the multi-entity side).

**What the tests do:**

```
Test Setup           Create Temporal Entity   # → POST /temporal/entities only

[Test body]:
    Delete Entity Attributes ...              # → DELETE /entities/{id}/attrs/{name}
    Retrieve Temporal Representation Of Entity timeproperty=deletedAt
    expect: the deleted attribute appears with `deletedAt`
```

**Why it's wrong:** `POST /temporal/entities` (§ 5.6.11) creates a
*Temporal Evolution of an Entity* — instances in the temporal store.
`DELETE /entities/{id}/attrs/{name}` (§ 5.6.5) is a *current-state*
operation that requires the entity to exist in the current-state
representation. The two stores are architecturally separate: a
temporal-only entity has no current-state attribute to delete, so
the spec-strict response is **404 Not Found**, and no `deletedAt`
tombstone is written.

The test fixtures assume — implicitly and undocumented — that the
implementation under test treats current-state and temporal as a
single unified entity (so a `POST /temporal/entities` magically also
creates the corresponding current-state entity, even though
§ 5.6.11.4 only says "create the provided Temporal Evolution of an
Entity", with no mention of current-state mirroring).

**Impact:** 13 tests fail in this family. Our broker keeps the two
representations separate by design; we will not mirror, so these
tests will keep failing.

**Fix wanted:** The `Test Setup` should also `POST /entities` (or
otherwise ensure the entity has a current-state representation with
the attribute to be deleted) before the test body runs. With that
change the `deletedAt`-tombstone path can be exercised faithfully,
without forcing the implementation into a unified-entity architecture
that the spec doesn't mandate.

---

## 10. `043_01_*` expects HTTP 503 for `LdContextNotAvailable`; spec says 504

**Hit:** `043_01_01`, `043_01_02`, `043_01_03`, `043_01_04`, `043_01_05`
— "Verify receiving 503 - LdContextNotAvailable error if remote
JSON-LD @context cannot be retrieved" across Create Entity / Create
Subscription / Create Temporal Representation / Batch Create / CSR
Create. All five hard-code:

```
${expected_status_code}=        503
```

**Where:** `TP/NGSI-LD/CommonBehaviours/CommonResponses/VerifyLdContextNotAvailable/043_01.robot`

**Why it's wrong:** § 6.3.4 Table 6.3.4-1 of v1.9.1 explicitly maps
`LdContextNotAvailable` to HTTP **504**, not 503:

```
https://uri.etsi.org/ngsi-ld/errors/LdContextNotAvailable    504
```

§ 6.30.3.2 (Delete and Reload) likewise pairs the same problem type
with 504 Gateway Timeout. There's no place in v1.9.1 that maps it to
503.

**Impact:** All five tests fail with `expected 503, got 504`. Our
broker also previously failed silently (returned 201) because the
URL-prefix shortcut for the broker's own core context was loose
enough to swallow `ngsi-ld-core-context-non-existing.jsonld` —
that's now fixed; we emit the spec-correct 504 + ProblemDetails.

**Fix wanted:** The fixture should set `${expected_status_code}=504`
and rename the test docstring to match. Until then the suite keeps
failing 5 tests for a spec-correct broker.

---

## 11. `051_07_01` extracts the full URL field instead of the localId for DELETE

**Hit:** `TP/NGSI-LD/jsonldContext/Provision/DeleteContext/051_07.robot`
"Delete A ImplicitlyCreated @contexts With A Valid Id And Reload Set To
True". Expects 400 BadRequestData; broker returns 404 ResourceNotFound.

**Where:** the test's setup keyword extracts the identifier with
```robot
${data}=    Get From List    ${response.json()}    0
${implicit_id}=    Get From Dictionary    ${data}    URL
```

That `URL` field in the List-@contexts response is the full broker URL
(e.g. `http://localhost:8080/ngsi-ld/v1/jsonldContexts/urn:ngsi-ld:Context:1-NNN`).
The companion test `051_06` does it correctly:
```robot
${implicit_id}=    Get From Dictionary    ${response.json()}    jsonldContext
${implicit_id}=    Evaluate    '${implicit_id}'.split('/')[-1]
```
i.e. takes the last path segment (the locally unique identifier).

**Why it's wrong:** § 5.13.5.3 says the operation takes "the locally
unique identifier that identifies the desired @context in the broker's
internal storage. For @contexts of kind 'Cached' this can also be the
original URL the broker downloaded the @context from." The URL form is
explicitly NOT supported for Hosted/Implicit, only Cached. So the test
ought to extract the localId (e.g. via the `localId` field of the list
entry, or by splitting `URL`).

**Impact:** broker correctly 404s (the URL-encoded full URL doesn't
match any cache key for an Implicit context), but the test expects 400
which would only fire after a successful lookup. One PASS→FAIL.

**Fix wanted:** mirror `051_06`'s pattern — take the `localId` field, or
split `URL` on `/` and take the last segment.


## 12. `019_09 / 019_10 / 019_11` use a self-intersecting polygon test fixture

**Hit:** the `Setup Initial Entities` keyword in
`TP/NGSI-LD/.../QueryEntities/019_09.robot`,
`019_10.robot`, `019_11.robot` (and any test that depends on
`building-location-polygon.jsonld` / `building-location-polygon-second.jsonld`)
creates a Building entity whose `location` GeoProperty is a Polygon with
crossing non-adjacent edges:

```
[[13.2865906,52.5648645],[13.2879639,52.5648645],
 [13.2797241,52.4988679],[13.477478,52.4712703],
 [13.5049438,52.5373084],[13.2865906,52.5648645]]
```

MongoDB's 2dsphere index rejects it with *"Loop is not valid: Edges 1
and 4 cross"*; per § 4.10 a self-intersecting polygon is not a valid
GeoJSON, so the broker now rejects the entity create with 400
BadRequestData (see swNgsild commit "ldCheckGeo: reject
self-intersecting polygons up front"). The test setup then fails and
every test in the suite is marked `FAIL`.

**Why it's wrong:** § 4.10 / RFC 7946 require polygon rings to be
simple (no self-intersection). The fixture is invalid GeoJSON and was
only "working" against brokers that didn't validate.

**Impact:** every 019_09 / 019_10 / 019_11 test fails on setup. Same
for `019_11_06`, where the *query* polygon is also self-intersecting —
that one would have failed even with a valid fixture.

**Fix wanted:** replace the polygon coordinates with a valid simple
polygon (e.g. a non-crossing pentagon roughly enclosing the same area
in central Berlin). Same for the query polygons in `019_11_06` and
similar.


## 13. `028_01_01 / 029_05_* / 029_06_01 / 030_03_01` deep-diff response against request — disallows spec defaults

**Hit:** these Subscription Create / Update / Retrieve tests build a
request payload, send it, then compare the response body to the
request via `deep_diff` and assert the result is empty.

**Why it's wrong:** § 5.2.12 makes `isActive` a 0..1 member with
default `true`. § 6.3.13 shows it on every Subscription representation
example. The broker injects `isActive: true` on create when the user
didn't supply one, and emits it on retrieve — so the spec-conformant
response contains a member the (minimal) request didn't.

The `deep_diff` then reports `dictionary_item_added: ["root['isActive']"]`
and the test fails. Same pattern would break for `status` (§ 5.2.12 too
— always emitted by the broker since it's computed, not user-supplied)
and any other defaulted member.

**Impact:** four tests flip PASS→FAIL purely from the broker emitting
`isActive: true`. Reverting the injection would also break the CSR-Sub
retrieve test that relies on the field being present, so the broker
side is correct.

**Fix wanted:** the assertion should ignore members that are spec
defaults / computed (`isActive`, `status`, `subscriptionName` when
auto-derived, etc.) — either with a `deep_diff` `exclude_paths`
allowlist or by switching to "subset" semantics ("response includes
every key the request asked for, with the same value").


## 14. `046_24 / 046_28` expectation files hard-code `createdAt`/`modifiedAt`

**Hit:** the JSON expectation files for `046_24_01` and `046_28_01`
(`entity-created-name-attribute-join-flat-sysAttrs.json`,
`entity-created-name-attribute-join-inline-sysAttrs.json`) include
fixed `createdAt`/`modifiedAt` ISO timestamps — e.g.
`"2025-05-19T15:20:16.418984Z"` — alongside the linked-entity members
the test cares about.

When `deep_diff` runs, the broker's *current-time* timestamps trip
`values_changed` and the test fails — even though the field that the
test was meant to verify (sysAttrs being present + linked entities
appearing) is satisfied.

**Why it's wrong:** the broker stamps `createdAt`/`modifiedAt` from
the request clock; no broker can satisfy a fixture that demands a
specific past datetime. § 4.5.4 / § 5.2.20 are explicit that these
are computed.

**Fix wanted:** strip `createdAt`/`modifiedAt` from the expectation
JSON files for these tests, OR move them through `deep_diff`'s
`exclude_regex_paths` so the timestamps are ignored.



## 15. `046_34_04` vs `046_37_01` LanguageProperty null-marker shape

**Hit:** when emitting the § 5.8.6 null-marker for a deleted
LanguageProperty, the two fixtures disagree on the wire form:

  * `046_34_04` (attribute-delete) expects
    ```json
    "street": { "type": "LanguageProperty",
                "languageMap": { "@none": "urn:ngsi-ld:null" } }
    ```
  * `046_37_01` (entity-delete + showChanges) expects
    ```json
    "street": { "type": "LanguageProperty",
                "languageMap": "urn:ngsi-ld:null",
                "previousLanguageMap": { fr: ..., nl: ... } }
    ```

Spec § 5.8.6 only says "the value (or object) shall be set to
`urn:ngsi-ld:null`". The bare-string form is the natural fit for
a single-value scalar key like `value` / `object` / `vocab` /
`json`, but for `languageMap` (whose normal shape is a JSON object)
both forms are defensible. The fixtures should pick one and stick
with it.

**Our call:** the broker emits `{@none: null}` on attribute-delete
notifications and bare `"urn:ngsi-ld:null"` on entity-delete
notifications, matching each fixture exactly. Both are spec-allowed.

**Fix wanted:** fixtures should agree (preferably both bare null,
since other types use bare null and § 5.8.6 reads more naturally
that way). Once aligned, the broker's two code paths can be
unified.


## 16. `051_02_01`, `051_04_02/03`, `053_03_01` — missing resource imports

**Hit:** these tests reference `${ERROR_TYPE_RESOURCE_NOT_FOUND}` in
`Check Response Body Containing ProblemDetails Element`, but their
`*** Settings ***` only import `jsonldContext.resource` and
`AssertionUtils.resource` (and HttpUtils for some). None of those
define the variable.

`${ERROR_TYPE_RESOURCE_NOT_FOUND}` is defined in
`Common.resource`, `ContextInformationConsumption.resource`, and
several others — just not in the ones these tests pull in.

Robot fails the test with `Variable '${ERROR_TYPE_RESOURCE_NOT_FOUND}'
not found.` before even checking the broker behaviour.

**Our call:** broker behaviour is correct (404 on unknown context-id);
nothing to fix on our side.

**Fix wanted:** add `Resource ${EXECDIR}/resources/ApiUtils/Common.resource`
(or any resource that defines the variable) to those four tests'
`*** Settings ***` blocks.


## 17. `051_02_01` — random numeric string is not a URI

**Hit:** the test generates a 16-digit random string with `Generate
Random String 16 [NUMBERS]` and DELETEs `/jsonldContexts/<digits>`,
expecting 404 ResourceNotFound. Per § 5.13.5.4 a context-id that is
not a valid URI must yield 400 BadRequestData — the broker correctly
returns 400.

**Our call:** broker enforces the URI check.

**Fix wanted:** the test should generate a URI (e.g.
`urn:test:<random>` or `http://example.org/<random>`) so the 404
path is exercised instead of the 400 (non-URI) path.


## 18. `051_05_01`, `053_05_01` — wants 503, spec says 504

**Hit:** both tests stop the local @context server, then hit the
broker with a path that triggers `LdContextNotAvailable` (e.g. DELETE
?reload=true on a Cached context whose source is gone). They assert
`Check Response Status Code 503` with reason "Service Unavailable".

ETSI GS CIM 009 v1.9.1 § 6.3.17 (and the table in ch6 line 260) maps
`LdContextNotAvailable` to **504** — `Gateway Timeout`. The broker
emits 504 per spec.

**Our call:** broker returns spec-mandated 504.

**Fix wanted:** the fixtures should assert 504 / "Gateway Timeout".


## 19. `051_07_01` — Robot's URL stripping mangles the implicit URL

**Hit:** the `Delete a @context` keyword strips the prefix
`/ngsi-ld/v1/jsonldContexts/` from an absolute URL. When the broker
returns the implicit context's URL as
`http://localhost:8080/ngsi-ld/v1/jsonldContexts/<id>`, the strip
leaves `http://localhost:8080<id>`, which Robot then URL-encodes and
sends as the path component. The broker decodes this, looks up an id
that doesn't exist, returns 404 — but the test expected 400 (reload
on Implicit).

**Our call:** broker behaves correctly given the input it receives.

**Fix wanted:** the strip should anchor on the absolute prefix
(e.g. regex `^https?://[^/]+/ngsi-ld/v1/jsonldContexts/`) or use the
last `/`-segment of the URL.


## 20. `051_08_*`, `051_09_*` — fixture `core_context` ≠ broker's actual core

**Hit:** `resources/variables.py` sets
`core_context = 'https://uri.etsi.org/ngsi-ld/v1/ngsi-ld-core-context-v1.6.jsonld'`.
The broker's actual core (compile-time) is v1.9 / v1.9.1. Per spec
(§ 5.13.5.4 / § 5.13.6.4), only the implementation's *actual* core
context is undeletable / un-reloadable — older or unrelated core URLs
are user contexts to the API and may be deleted normally.

So when the test does `Delete a @context ${core_context}` expecting
400 (or 200 for 051_09 reload), the broker (correctly) returns 204 /
404 because v1.6 is just a regular Cached/Implicit context to it, not
the core.

**Our call:** keep the spec-strict semantics. Don't extend the
"undeletable core" classification to alternate-version core URLs.

**Fix wanted:** `variables.py` should be set per-implementation, e.g.
`core_context = 'https://uri.etsi.org/ngsi-ld/v1/ngsi-ld-core-context-v1.9.jsonld'`,
or the fixture should derive it from a broker-served endpoint.


## 21. DistOps — `application/ld+json` + `Link` header in body-bearing POSTs

**Hit:** the Robot keyword `Query Entities Via POST` (and the related
batch-query helpers used across `D011_*`, `D016_*`) sets
`Content-Type: application/ld+json` AND a `Link: <ctx>; rel=…/json-ld#context`
header, with no `@context` in the body. The broker returns 400
BadRequestData ("Missing @context") and the test fails with
"expected 200, got 400" (~6 tests).

**Spec:** § 6.3.5 is explicit:
> "the presence of a JSON-LD Link header in the incoming HTTP request
>  when the Content-Type header is application/ld+json shall result
>  in an HTTP error response of type BadRequestData."

So broker behaviour is mandatory.

**Our call:** broker stays spec-strict — both "ld+json without body
@context" and "ld+json + Link header" raise 400.

**Fix wanted:** the test framework's POST keywords should pick ONE
of two consistent shapes:
  - Content-Type `application/json` + Link header (no body @context); or
  - Content-Type `application/ld+json` + body @context (no Link header).
Currently they emit a hybrid that the spec mandates rejecting.


## 22. DistOps — HttpCtrl stub literal-match doesn't match forwarded URL

**Hit:** `Set Stub Reply <method> <url> <status> <body>` registers an
HttpCtrl stub keyed on the URL string `/broker1/ngsi-ld/v1/...` (or
similar). The broker, when forwarding a distributed operation to the
mocked Context Source, appends URL parameters mandated by § 5.7.2 /
§ 5.6.x — typically `?type=<expanded>&pick=<expanded>&sysAttrs=true`,
plus `id=` lists for queries. HttpCtrl matches stubs by exact URL
string, so the request misses the stub. The mock then either:
  - returns nothing (timeout → 7 tests fail with `Timeout: request
    was not received`), or
  - returns a default 404/empty (broker reports CS forward failed →
    207 instead of 204, 502 instead of 200 — ~30+ tests).

**Spec:** § 5.7.2 entitles the broker to attach the matching CSR's
property/relationship constraints as query params on the forward
(`pick`, `omit`, `type`, etc.). § 4.5 / § 5.7.1 say sysAttrs and
similar shall be honoured end-to-end. Broker is correct.

**Our call:** broker emits canonical distop URLs.

**Fix wanted:** stubs should match by URL prefix or use a regex.
Robotframework-httpctrl supports `Set Stub Reply` with regex match
in newer versions — adopting that across the DistOps suite would
flip ~30 tests without touching the broker.


## 23. DistOps — `Get Stub Count 1 (integer) != 1 (string)` (~12 tests)

**Hit:** Robot tests do `Should Be Equal ${stub_count} 1`.
HttpCtrl's `Get Stub Count` returns a Python int; the literal `1`
in the .robot file is a string. Robot's `Should Be Equal` is type-
strict and fails.

**Spec:** N/A.

**Our call:** broker uninvolved.

**Fix wanted:** use `Should Be Equal As Numbers` / `Should Be Equal
As Integers`, or `Should Be True ${stub_count} == 1`.


## 24. DistOps — `No keyword with name 'Get Request Url Params' found`

**Hit:** three tests reference `Get Request Url Params`, a keyword
that doesn't exist in the version of robotframework-httpctrl
installed for the test runner. Tests crash before the broker ever
sees a request.

**Our call:** broker uninvolved.

**Fix wanted:** pin a robotframework-httpctrl version that exposes
this keyword, or rewrite the affected tests with what the installed
version offers.


## 25. § 5.10.2.5 / 037_08, 037_09_*, 037_10_02 — ETSI fixtures expect the un-filtered RegistrationInfo set

**Hit:** Eight `GET /csourceRegistrations?…` tests (037_05_01,
037_08_01, 037_09_01..04, 037_10_02, 037_11_*) assert against an
expectation file that contains *every* `information[]` entry of the
created CSR, regardless of whether the entry's `entities` / property
names matched the discovery filter.

The broker today returns only the matching entries (i.e. `entry.entities
∩ ?type/?id/?attrs ≠ ∅`), so the response is a subset of what the
fixture lists → `Compare Dictionaries Ignoring Keys` reports the
missing entries as `Item …['information'][N] removed from iterable`.

**Spec:** § 5.10.2.5 says implementations **should** return filtered
registrations — only matching RegistrationInfo elements. "Should",
not "shall" — so both shapes are spec-compliant.

**Our call:** keep the filtered behaviour by default — it's the
spec-recommended thing and a cleaner client experience (no irrelevant
entries returned). Provide `--testConformance/-tc` so ETSI runs can
opt into the un-filtered shape and pass these eight tests.

**Fix wanted:** ETSI fixtures should accept either shape, or — better
— the spec should add a URL param to let the client choose (see
spec-doubts § 26 for the proposal).


## 26. § 5.10.2.4 / 037_10_01 — three separate bugs in one test (one was on broker side, two on fixture side)

The test calls `GET /csourceRegistrations?id=<csr1>,<csr3>` and expects 200 + two specific CSRs.

### 26a. Too-wide rejection on `?id=` alone — RESOLVED broker-side

§ 5.10.2.4 lists `type / attrs / q / geoQ` as the required sufficient selectors. `id` and `idPattern` are not in that list verbatim, but they bound the candidate set at least as tightly. Broker now accepts `?id=` and `?idPattern=` as sufficient selectors for CSR Discovery, mirroring the same relaxation already applied to `/entities`. Match function handles comma-separated `?id=A,B` as OR-of-any.

Spec gap still worth raising with ETSI TC DATA — § 5.10.2.4 should mention id / idPattern.

### 26b. `?id=` semantic — fixture confuses CSR's own id with EntityInfo.id

Per § 5.10.2, `?id` filters on `EntityInfo.id` (entities the CSR claims to know about), **not** on the CSR's own identifier. The CSRs in this test are created with `entities: [{ "type": "Building" }]` — no `id` field — so the EntityInfo-id constraint is "any". Filtering on `?id=urn:ngsi-ld:ContextSourceRegistration:X` doesn't narrow anything; the broker returns all CSRs with matching information.

The test author has mistaken the EntityInfo-id filter for a CSR-id filter. Either:
- the fixture should query by something the EntityInfo actually has (a specific entity id, a type), or
- spec should be amended to also support a filter on the CSR's own id (proposal-worthy — convenient for fetch-by-id when caller has discovered them earlier).

Broker is spec-correct here.

### 26c. Expected body is malformed — template never substituted

The expectation file looks like Robot string-formatted `${first_id},${third_id}` straight into a JSON dict key, producing one key `"urn:ngsi-ld:CSR:A,urn:ngsi-ld:CSR:B"` (with literal comma) instead of two keys. Adjacent entries also show unfilled `urn:ngsi-ld:ContextSourceRegistration:randomUUID` placeholders. **No broker can pass this body assertion** — the fixture is shipped broken.

**Fix wanted:** regenerate the expectation file with proper id substitution; once that's done, fix 26b by changing the query to a meaningful filter (e.g. `?type=Building`).


## 27. § 5.5.9 / 037_11_01 + 037_11_02 — pagination expects offset to index page-by-page, not item-by-item

**Hit:** Setup creates 3 CSRs with two different fixtures; `?type=Building` matches 2 of them. The two tests then query with limit/offset:

- 037_11_01: `limit=1, offset=2` expects **1** result.
- 037_11_02: `limit=2, offset=2` expects **1** result.
- 037_11_03: `limit=15, offset=0` expects **2** results — passing.

The 03 fixture confirms the matching-set size is 2. With § 5.5.9's zero-based item offset:
- offset=2, limit=1 → skip 2 of 2 items → 0 results.
- offset=2, limit=2 → same → 0 results.

For the tests to expect 1, the fixtures must either (a) count all three CSRs as matching `?type=Building` (one of them has no Building EntityInfo so this is wrong), or (b) interpret offset as a page index multiplied by limit (which contradicts § 5.5.9).

**Our call:** broker is spec-strict on offset.

**Fix wanted:** rewrite the fixtures with an offset and matching-set count that line up, or — if 037_11_03's "expects 2" is actually correct — pick offset values that don't exceed the matching-set size.


## 28. § 5.7.2.4 / D011_03_inc_01/02 + D011_04_inc_01/02 — `?id=urn:…` alone rejected as "too wide"

**Hit:** Four distributed-query tests call `GET /entities?id=urn:ngsi-ld:Vehicle:<uuid>` (and the queryBatch POST equivalent) with no `type`, `attrs`, `q`, geoQ, `scopeQ`, or `local=true`. Expects 200 with the entity from the matching CSR.

**Spec:** § 5.7.2.4 enumerates exactly five sufficient selectors:
- (a) selector of Entity Types
- (b) list of Attribute names with at least one non-system Attribute
- (c) NGSI-LD Query with at least one non-system Attribute
- (d) NGSI-LD GeoQuery
- (e) local scope

`id` and `idPattern` are NOT in that list. The broker rejects 400 "too wide query (§ 5.7.2.4 — id / idPattern alone is too wide)" per `ldParamsValidate.c`.

**Broker:** stays spec-strict — the listing in § 5.7.2.4 is enumerative ("the following input data shall be provided"), and `id` is conspicuously absent. A `getEntity by id` operation already exists (`GET /entities/{id}` — single entity, no query semantics); the queryEntities path is for filtering, where unbounded id-filter has no use.

**Our call:** broker correct, tests wrong. The CSR Discovery variant of this (§ 26a) WAS relaxed because there the `id` filter applies to EntityInfo.id, not to the resource being listed — different semantics.

**Fix wanted:** the four tests should each add a `&type=Vehicle` or use `GET /entities/{id}` directly. Either passes a sufficient selector while still exercising the CSR forward.


## 29. § 5.5.9 / 041_03_01..03 — `?page=` is not a NGSI-LD pagination parameter

**Hit:** Tests 041_03_01, 041_03_02, 041_03_03 query CSR subscriptions with `GET /csourceSubscriptions?limit=1&page=2` (and `page=3`). Expect 200 with the entity at that page.

**Spec:** § 5.5.9 / § 6.3.13 define pagination via `limit` (page size) and `offset` (zero-based item index). There is no `page` URL parameter anywhere in the NGSI-LD 1.9.1 spec — pagination is item-index-based, not page-number-based.

**Broker:** rejects 400 InvalidRequest "Unknown/unsupported URL parameter: page". Correct per § 6.3.4 (unknown URL params must be rejected for body-bearing endpoints; for read endpoints the broker is also strict, which matches § 5.5.9's enumerated list).

**Fix wanted:** the test fixtures should use `offset=2` / `offset=3` / etc. — actual NGSI-LD pagination — instead of `page=N`.
