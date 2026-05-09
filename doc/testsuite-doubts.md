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

