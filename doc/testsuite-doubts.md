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

## 10. `043_01_*` + `028_07_02` expects HTTP 503 for `LdContextNotAvailable`; spec says 504

**Hit:** `043_01_01`, `043_01_02`, `043_01_03`, `043_01_04`, `043_01_05`
— "Verify receiving 503 - LdContextNotAvailable error if remote
JSON-LD @context cannot be retrieved" across Create Entity / Create
Subscription / Create Temporal Representation / Batch Create / CSR
Create. Plus `028_07_02` (subscription create with unreachable
`jsonldContext` URL — same pattern, asserts 503). All hard-code:

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


## 30. D001_04_inc — Robot keyword typo `Generate Random Vehice Id`

**Hit:** Test D001_04_inc setup calls `Generate Random Vehice Id`. Robot reports: `No keyword with name 'Generate Random Vehice Id' found. Did you mean: 'Generate Random Vehicle Entity Id'`.

**Spec:** N/A — pure typo.

**Fix wanted:** rename the call to `Generate Random Vehicle Entity Id` (the actual keyword in `Common.resource`).


## 31. D001_02_exc / D002_02_exc / D012_01_exc — HttpCtrl `OSError` starting mock server

**Hit:** Setup fails with bare `OSError`. The traceback (in the log.html) points at `HttpCtrl.Server.Start Server` — likely the mock can't bind its port (timing / leftover socket from a previous test).

**Spec:** N/A.

**Fix wanted:** make `Start Context Source Mock Server` resilient to bind failures (retry, or rebind on a different port). Today a single port-clash in any test cascades the entire setup. Same root cause as §22 (HttpCtrl mock fragility).


## 32. 047_02_01 — fixture assertion compares two different CSR ids

**Hit:** Setup-phase `Should Contain` checks that one CSR id list contains another id; they're unrelated (`[A]` should contain `B`). The setup fails immediately. The test is in ContextSourceRegistrationSubscriptionNotificationBehaviour, which has multiple Setup keywords that each generate fresh CSR ids — the assertion appears to use the wrong variable.

**Spec:** N/A — fixture variable bug.

**Fix wanted:** trace the variable indirection in the setup keyword chain and align the asserted ids.


## 33. 053_06_01 — fixture sends path-only context id `/api/v1/context.jsonld`

**Hit:** Setup retrieves an implicitly-created context via path `/api/v1/context.jsonld` (no scheme, no host). Broker (correctly per § 5.13) rejects with 400 BadRequestData "context id '/api/v1/context.jsonld' is not a valid URI". Fixture expects 200.

**Spec:** § 5.13 mandates context ids be URIs (scheme://...). A bare path is not a URI.

**Fix wanted:** the fixture should generate the full URL (broker base + path) before issuing the GET.


## 34. 019_11_* — fixture polygon is self-intersecting per S2 geometry

**Hit:** Suite setup of `QueryEntities.019 11` creates an entity with a
5-vertex polygon whose `BC` and `EA` edges cross near vertex `A`. Mongo's
2dsphere index (which uses S2 geometry) rejects with "Can't extract geo
keys"; the entity creation returns 400 (was 500 before §34's broker fix)
and all 8 sub-tests in the suite cascade-fail.

Polygon coordinates: `[[13.2865906,52.5648645], [13.2879639,52.5648645],
[13.2797241,52.4988679], [13.477478,52.4712703], [13.5049438,52.5373084],
[13.2865906,52.5648645]]`. The first two vertices are essentially collinear
at y=52.5648645, then BC goes far SW and EA (the closing edge) cuts back
across BC just before reaching A.

**Spec:** § 4.7.1 says polygons "should" not be self-intersecting (SHOULD,
not MUST), so technically valid input — but any storage layer using S2
(which most do) will refuse to index it.

**Fix wanted:** pick a different polygon for these geo-query tests, or
explicitly state the polygon contract is "S2-valid GeoJSON". Until then
the seven 019_11_* sub-tests all fail at suite-setup.


## 35. 046_24_01 / 046_28_01 — fixture compares createdAt/modifiedAt as literals

**Hit:** Notification-with-linked-entity tests compare the broker's
response against an expectation file containing hardcoded
`"createdAt": "2025-05-19T15:20:16.418984Z"` / `"modifiedAt": "..."`. Since
the test creates the entity at test-run time, the broker's actual timestamps
will always differ — both at the entity level and inside the embedded
`name`/`locatedAt` attributes.

**Spec:** N/A.

**Fix wanted:** the expectation file should mark these fields as
"any value" / regex, or the assertion utility should skip them.


## 36. 038_01_01 / 039_01_01 / 040_01_01 — CSR-Subscription fixtures
don't expect notificationTrigger or notification stats

**Hit:** Create/Update/Retrieve of a CSR-Subscription. ETSI expectation
for the retrieved sub:
- `isActive: true` present (broker now suppresses defaults; 028_06 expects suppression)
- No `notificationTrigger` (broker now default-emits per § 5.2.12)
- No `notification.timesSent`/`timesFailed`/etc. (broker emits stats per
  § 5.2.14)

028_06 (regular Subscription) expects the OPPOSITE: no isActive, yes
notificationTrigger default. So ETSI's own fixtures disagree on the
spec-default-emission rules across the two endpoints.

**Spec:** § 5.2.12 — defaults aren't supposed to be persisted as user
fields; § 5.2.14 — notification counters are part of the Subscription
representation.

**Fix wanted:** decide and apply consistently across all subscription
retrieve fixtures (regular + CSR). Both endpoints share the Subscription
resource type, so behaviour shouldn't diverge.


## 37. Robot teardowns clean current-state but NOT temporal history

**Hit:** Run-to-run variance of ±10–15 tests on the full ETSI suite,
all concentrated in the **021_* (temporal)** cluster, all showing as
`200 != 206` from the broker. Cherry-picking a single failing
021_* test in isolation makes it pass — the failure only reproduces
inside the full suite run.

**Why:** every `Test Teardown` in the ETSI Robot suite does
`DELETE /ngsi-ld/v1/entities/{id}` (current-state cleanup) but does
NOT also `DELETE /ngsi-ld/v1/temporal/entities/{id}` (history
cleanup). TRoE rows from every prior test linger for the entire run.
By the time the temporal tests run, the per-entity attribute-instance
history (across all tests sharing the broker's timescale DB) exceeds
the broker's `-troeCap` (default 100, § 6.3.10). The TRoE driver
correctly returns 206 + Content-Range; the test expected 200.

**Spec:** § 6.3.10 — 206 + Content-Range is the right answer for a
truncated temporal result. Spec-correct broker, fixture-side problem.

**Why this matters beyond the count drift:** the suite is no longer
order-independent. Cherry-picking a single 021_* test to debug a
failure works (clean DB → fits under the cap → passes), but the
exact same test fails when run inside the suite (cap blown by prior
fixtures). That makes triage of any temporal failure unnecessarily
expensive.

**Fix wanted:** add `DELETE /ngsi-ld/v1/temporal/entities/{entity_id}`
to every test teardown that creates temporal data, paired with the
existing entity-delete call. The broker fully supports the temporal
DELETE; teardown order doesn't matter (current and temporal are
independent stores).

**Workaround on the broker side:** start the broker with
`-troeCap 100000` (or larger) for ETSI baseline runs — pushes the
cap above what any reasonable accumulation hits. We're not enabling
this by default in our broker because the spec-correct cap is part
of what the suite ought to exercise.

## 38. 041_04_02 / 041_04_03 — fixture uses non-spec `?page=` param

**Test:**
[041_04.robot](https://forge.etsi.org/rep/cim/ngsi-ld-test-suite/-/blob/develop/TP/NGSI-LD/ContextSource/RegistrationSubscription/QueryContextSourceRegistrationSubscriptions/041_04.robot) — "Check that one cannot query context source registration subscriptions with invalid page and limit parameters".

```
041_04_01 Invalid Limit            limit=-5   page=2
041_04_02 Invalid Page             limit=2    page=-3
041_04_03 Invalid Limit And Page   limit=0    page=0
```

All three assert `400 BadRequestData`.

**Broker:** returns `400 InvalidRequest` for the two cases that
include `?page=` (test names ending _02 and _03), because `page` is
not a registered URL parameter and the broker correctly classifies an
unknown URL parameter as InvalidRequest per § 6.3.20.

**Spec:** NGSI-LD defines two pagination parameters — **`limit`**
(§ 6.3.10 Table 6.3.10-1) and **`offset`** (§ 6.3.10 Table 6.3.10-2).
There is no `page` parameter. § 6.3.20: "If an HTTP request for an
operation contains parameters that are incompatible with the
operation … an HTTP error response of type InvalidRequest should be
returned." So `?page=…` is unknown → InvalidRequest, not
BadRequestData.

**Fix wanted:** rewrite the fixture to exercise only spec-defined
pagination parameters. The intent ("reject negative pagination") is
correct and worth keeping, but it should use `offset=-3` (the actual
NGSI-LD analogue of "page") to validate the broker's value-range
rejection on a known parameter, which IS BadRequestData per § 5.5.9.
Alternatively, the test can keep `?page=` and reclassify its expected
error to InvalidRequest.

**Note:** 041_04_01 uses only `limit=-5` (no `page=`) and expects
BadRequestData. That's consistent with the spec and our broker
returns BadRequestData for it.

## 39. 059_01_* + 007_02_02 + 015_01_01 (AddAttribute) + 050_02_02 — BadRequestData vs InvalidRequest split

**Test pattern:** ETSI tests across several sections assert
**InvalidRequest** for what § 5.5.4 / § 6.3.20 classify as
syntactic/transport errors (as opposed to semantic errors in a
parsed NGSI-LD payload, which are BadRequestData):

- **059_01_xx** — `?invalidParams=invalidValue` on every endpoint
  family. § 6.3.20: unknown URL param → InvalidRequest.
- **007_02_02 Create A Temporal Entity With An Empty Json** —
  empty request body. § 5.5.4: "not a valid JSON document" →
  InvalidRequest.
- **015_01_01 (setup) Append Attribute With Empty Body** — same as
  above.
- **050_02_02 Checking Wrong JSON** — POST `/jsonldContexts` with
  malformed JSON. § 5.5.4 again — not a valid JSON document.
- **050_02_01 Checking Incorrect Payload** — POST
  `/jsonldContexts` with a JSON object whose shape is wrong
  (missing required key). Our broker returns InvalidRequest here
  because the payload is rejected at the syntactic-shape gate
  ("payload must be a JSON object") *before* any semantic
  @context-content validation. The test agrees.

No fixture changes needed — this entry exists so future readers
understand the rationale for the broker behaviour. The 11 fw
failures + the symmetric 1 sw failure for these tests cleared after
the broker stopped over-using BadRequestData on transport-layer
issues.

## 40. HttpCtrl mock — `Set Stub Reply` URL is exact-match (path + query string)

**Library:** `robotframework-httpctrl` (used by all DistOps tests via
`resources/MockServerUtils.resource` → `Start Context Source Mock Server`).

**Behaviour:** in `HttpCtrl/http_stub.py::HttpStubCriteria.__eq__` the
stub is matched against an incoming request only when both `method`
*and* `url` are case-insensitively equal. The "url" is the request's
raw target — path **and** query string, no normalisation. A stub
registered as `Set Stub Reply  POST  /a/b/attrs/` will *not* match a
request to `/a/b/attrs` (missing trailing slash) or
`/a/b/attrs/?sysAttrs=true` (extra query string).

**Concrete impact on the swBroker / fwBroker DistOps tests:**

1. **Trailing-slash mismatch on the attribute-list endpoint.** § 6.6.3
   Table 6.6.3.1-1 shows the URI template as `/entities/{entityId}/attrs/`
   (with trailing slash). Many test stubs include the trailing slash;
   our broker used to send `/attrs` (no slash) on forwarded requests.
   D003_01_red, D004_01_red, D006_02_exc, D014_01_red, D014_02_red all
   failed with `204 != 404` because the mock never matched the
   stub → mock fell back to the default reply → forward "failed" from
   the broker's perspective → `anyCsrSucceeded` stayed false → broker
   returned 404 "entity not found".

   **Worked around** on the broker side (postEntityAttrs / patchEntityAttrs /
   postEntityTemporalAttrs now emit the trailing slash) but the
   underlying mock-matching bug stays.

2. **Forward URL carries `?sysAttrs=true` and `&type=…`.** For
   retrieveEntity through CSRs the broker has to ask the upstream for
   sysAttrs (createdAt / modifiedAt are needed at the merge tiebreaker
   per § 4.5.5.3) and the entity type is added when the CSR's
   RegistrationInfo specifies one. Both are correct per § 5.7.1 / §
   4.3.6.3. The ETSI stubs are registered with `Set Stub Reply  GET
   /ngsi-ld/v1/entities/{id}  200  …` — no query string — so the
   stub never matches the broker's request.

   This affects:
     * D010_01_inc, D010_01_red (single-CSR retrieve)
     * D010_03_inc_01..03 (chained retrieve)

   **Not worked around** — dropping `sysAttrs=true` from the forward
   would silently break the spec-mandated merge for multi-source
   reads. The fix belongs on the testsuite side.

**Fix wanted upstream:** either
  (a) loosen `HttpStubCriteria.__eq__` to compare just the path
      (with optional `?…` glob support), or
  (b) update every affected `Set Stub Reply` to register both the
      canonical URL and a `?sysAttrs=true` variant (cumbersome), or
  (c) switch tests that need precise URL matching to `Wait For
      Request` + manual reply instead of the stub mechanism.



## 41. `020_14_01/02` — default temporal page size is not in the spec

**Hit:** 60-instance fixture (`speed` Jan 1, `fuelLevel` Jan 2)
queried with `timerel=after&timeAt=2019-01-01`. Test expects
`fuelLevel` to come back empty — the implicit assumption is the
broker applies a default pagination on temporal retrieves that
sorts the union of all instances by `observedAt` and truncates
at some threshold < 118.

**Spec:** § 6.3.10 doesn't pin a default temporal page size. swBroker
returns all 118 instances → the `Check Data Is Empty` assertion on
`fuelLevel` fails.

**Why it's wrong:** two defensible interpretations exist; the
fixture is committed to one without spec backing.

**Fix wanted:** either nail down a default page size in the spec,
or rewrite the test to assert pagination via an explicit `lastN=`.


## 42. `021_25` — expectation file missing one of the entities

**Hit:** `Setup Initial Temporal Entities` creates `Vehicle:021-06-A`
and `Vehicle:021-06-B`. The query `?local=true&timerel=after&timeAt=
2020-07-01` matches both. Expectation file
`data/temporalEntities/expectations/vehicles-temporal-representation-021-06.jsonld`
contains only entity A.

Test is tagged `not-implemented` already, so the author knew it
was WIP. Broker correctly returns both → deepdiff complains
`Vehicle:021-06-B added to dictionary`.

**Fix wanted:** add entity B to the expectation file (or drop the
test's `not-implemented` tag once the fixture is repaired).


## 43. `047_03/04/08/09/16` — CSR-sub vs CSR fixture entity-type mismatch

**Hit:** the CSR-subscription notification tests pair
`csourceSubscriptions/subscription.jsonld` (`entities: [type:
Building]`) with `csourceRegistrations/context-source-registration.jsonld`
(`entities: [type: Vehicle], [type: OffStreetParking]`). The CSR
doesn't overlap with the sub's entity scope, so per § 5.11.7 no
`newlyMatching` notification should fire.

**Broker:** correctly fires nothing in isolation → test times out
("Timeout: request was not received"). In the full suite the test
sometimes "succeeds" structurally because a Building CSR created by
another test lingers in the regCache and its id ends up in the
notification body, then deepdiff complains `<old-id> does not
contain <expected-new-id>`.

**Fix wanted:** rework the fixture pair so the CSR entities match
the subscription's `entities[type:Building]` (or drop the `entities`
filter from the sub if the intent is "any new CSR").


## 44. `038_08_03 InvalidQuery` — bare attribute name is a valid q

**Hit:** fixture `csourceSubscriptions/subscription-invalid-query.jsonld`
sets `q: "invalidQuery"` and the test asserts 400 BadRequestData.

**Spec:** § 4.9 — a bare attribute name in q is the "attribute
exists" predicate. Perfectly valid.

**Broker:** correctly returns 201.

**Fix wanted:** rewrite the fixture's `q` to something genuinely
invalid (mismatched parentheses, unsupported operator, etc.), or
drop the test.


## 45. `041_01_01 / 041_02_03 / 038_02_01` — fixture omits `notificationTrigger` (API-version drift)

**Hit:** CSR-subscription create/retrieve fixtures omit
`notificationTrigger` in both the POSTed body and the retrieval
expectation file.

**Spec:** `notificationTrigger` is a § 5.2.12 field that appears
in newer NGSI-LD spec revisions (post-1.6.1). When omitted, the
default is `["attributeCreated", "attributeUpdated"]`. Brokers
surface that default on retrieve so the user sees what's in force.

**Broker:** does that — adds `notificationTrigger:
["attributeCreated","attributeUpdated"]` on retrieve when the
user didn't provide one. The deepdiff fails with
`'notificationTrigger' added to dictionary`.

**Why it's a doubt:** the test fixtures look authored against an
older spec revision (1.6.1 or earlier) where the field didn't
exist. Newer-broker correctness collides with older-test
expectations.

**Fix wanted:** update the affected fixtures + expectations to
the current spec, OR drop deep-diff in favour of asserting only
user-set fields.


## 46. `002_02_01 / 054_02_02 / 056_02_02` — confused expectations for verbs on `/entities/`

**Hit:** three fixtures hit the entity collection endpoint with a
write verb and no id, each expecting a different status:

- `002_02_01` — `DELETE /entities/` — expects **405**
- `054_02_02` — `PUT /entities/`    — expects **400**
- `056_02_02` — `PATCH /entities/`  — expects **400**

**Spec:**
- `DELETE /entities` IS a real endpoint (§ 6.4.3.3 — Purge
  Entities). Purge was added post-1.6.1, so the test fixture
  (likely authored against pre-purge spec) treats it as
  non-existent and expects 405. The broker correctly handles
  purge and returns 400 with "requires at least one of
  id/type/idPattern/q/geoquery/scopeQ or ?local=true" — the user
  supplied no filter.
- `PUT /entities` and `PATCH /entities` are NOT defined on the
  collection — only on the individual `/entities/{id}` resource.
  HTTP convention says 405 Method Not Allowed.

**Broker:** consistent and correct on all three (400 for purge,
405 for the two undefined verbs).

**Fix wanted:** the three fixtures need consistent + spec-aware
expectations:
- `002_02_01`: change to 400 (purge endpoint, missing filter) —
  OR keep at 405 and add a filter so the response really IS 405.
- `054_02_02`, `056_02_02`: change to 405.


## 47. `016_02_06` — PATCH temporal attr instance with empty attr name

**Hit:** `PATCH /temporal/entities/{id}/attrs//{instanceId}` (note
the empty attr-name segment). Test asserts **405**.

**Broker:** routes the request via wildcard matching to the
partial-update-temporal-attr handler, validates the empty attr
name as a § 4.6.2 violation, returns **400** "invalid attribute
name '' (§ 4.6.2)".

**Why it's a doubt:** both interpretations are defensible — 405
from a strict router perspective (URL doesn't match a valid
route shape), 400 from a "URL matched, but the name slot is
invalid" perspective. 400 is more diagnostic.

**Fix wanted:** decide on a convention and update the test or
the broker. Same logic should also apply to other
attrs/{empty}/... routes (016_02_05 is the same shape but uses
`invalid(Id` instead of empty and asserts 400 — broker matches).


## 48. `034_05_01` — fixture uses real JSON null to unset `expiresAt`

**Hit:** `PATCH /csourceRegistrations/{id}` with body
`{ "expiresAt": null }` (real JSON null). Test expects **204**
and afterwards `expiresAt` to be gone from the registration.

**Where:**
`data/csourceRegistrations/fragments/context-source-registration-null-expiresAt.json`.

**Spec:** § 4.5.21 introduces the explicit sentinel
`"urn:ngsi-ld:null"` to mean "delete this member" in a merge
patch. Real JSON null is NOT the agreed signal for that — and
§ 7.4.4 of JSON-LD (Expansion algorithm) explicitly drops
members whose value is `null` *before* the request body is
interpreted. So a JSON-LD-conformant pre-processor sees
**no** `expiresAt` member at all — the merge becomes a no-op.

**Broker:** rejects with **400** "'expiresAt' must be a DateTime
string". Correct per § 4.5.21 — null is not a valid
DateTime, and the sentinel `"urn:ngsi-ld:null"` was designed
precisely to make this case unambiguous.

**Fix wanted:** the fixture should send
`{ "expiresAt": "urn:ngsi-ld:null" }` (with `Content-Type:
application/json` — the sentinel survives JSON-LD expansion
because it is a string, not null). The broker is correct.


## 49. `001_05_02 / 003_04_02 / 003_05_02 / 010_04_01 / 033_05_01` — fixtures key on the expanded IRI in plain-JSON responses

**Hit:** the test code reads response keys with the fully-
expanded JSON-LD IRI:
```python
${response_body['ngsi-ld:default-context/almostFull']}
${response_body['https://ngsi-ld-test-suite/context#almostFull']}
${response_body['ngsi-ld:default-context/attribute_to_be_added']}
${response_body['ngsi-ld:default-context/Building']}
```
Each fails with `KeyError` because the broker compacts the
response back to short names (`almostFull`, `Building`) as
spec wants.

**Spec:** § 6.3.5 — `application/json` responses are compacted
using the user @context. Short names are correct on the wire.

**Broker:** correct (compacts).

**Fix wanted:** Robot tests should access
`${response_body['almostFull']}`, `Building`, etc. — i.e.,
the short names that JSON-LD compaction produces.


## 50. `020_05_02 / 020_13_06..09 / 021_15_05..07` — temporal 206 vs 200

**Hit:** broker emits **206 Partial Content** + `Content-Range`
when the temporal slice exceeds the per-entity instance cap
(`--troeInstanceCap`, default 20 per § 6.3.10). Tests expect
**200** in cases where the slice still fits, OR **206** in
cases where the slice fits but the test predates the cap rule.

The result: each test in this family encodes one specific
interpretation of when 206 should fire, but the broker (using
a uniform "instance count > cap" rule) crosses the test's
expectation at different points.

**Spec:** § 6.3.10 / § 6.3.5 — the broker SHALL emit 206 +
Content-Range when an entity's instance count exceeds the
configured pageSize. The configured pageSize is broker-
dependent; the test suite hard-codes one specific value.

**Broker:** correct under its own configured cap. Same behaviour
as doubt #41 (`020_14_01/02` — default temporal page size not
in the spec).

**Fix wanted:** either the test suite needs a documented
expected-cap that brokers honour, or the assertion accepts
both 200 and 206 + Content-Range for these cases.


## 51. `054_01_01 Replace An Existing Entity` — modifiedAt jitter on exact-string compare

**Hit:** the assertion compares `modifiedAt` strings byte-for-
byte:
```
2026-05-20T15:22:04.844856025Z != 2026-05-20T15:22:04.823397351Z
```
The two timestamps differ by ~20 ms because the broker records
`modifiedAt` at the moment of write, then the test reads back
and compares to a fixture timestamp generated at a different
moment.

**Spec:** § 4.5.10 — `modifiedAt` is broker-assigned; the
client cannot predict its exact value.

**Broker:** correct (writes its own timestamp).

**Fix wanted:** the test should compare with tolerance (e.g.
ignore `modifiedAt` and other server-side timestamps in the
deep-diff), or assert "is a valid DateTime, is close to now".


## 52. `047_03_01 / 047_04_01 / 047_08_01 / 047_09_01 / 047_16_01` — hardcoded CSR ids in assertion

**Hit:** these CSR-subscription tests assert against a CSR id
that's hardcoded in the expectation file, e.g.:
```
[ urn:ngsi-ld:ContextSourceRegistration:5902198644784923 ] does not contain value
'urn:ngsi-ld:ContextSourceRegistration:5902198644784923'
```
But the setup generates a fresh random CSR id each run
(`Generate Random CSR Id`). The expectation file is never
re-written with the random id; the comparison can therefore
never match.

**Spec:** N/A — purely a test-fixture wiring issue.

**Broker:** correct (notification fires with the actually-
registered CSR id).

**Fix wanted:** the assertion needs to substitute the
generated CSR id into the expectation (the same trick the
test does for entity ids elsewhere), or the expectation file
needs to be templated.


## 53. `047_05_01 / 047_06_01` — fixture predates `timesFailed` and `status` fields

**Hit:**
```
Item root['timesFailed'] added to dictionary.
Value of root['status'] changed from "ok" to "failed".
```
The broker now emits `timesFailed` (and reflects the success/
failure of the last notification attempt in `status`). Older
expectation files don't list these.

Same shape as doubt #45 (`041_01_01 / 041_02_03 / 038_02_01`
— fixtures omit `notificationTrigger`).

**Broker:** correct (per § 5.11 the CSR-sub maintains delivery
stats; the spec doesn't forbid surfacing them).

**Fix wanted:** regenerate the expectations against a current-
spec broker.


## 54. `051_04_03` — fixture references an undeclared Robot variable

**Hit:** assertion fails with
```
Variable '${ERROR_TYPE_RESOURCE_NOT_FOUND}' not found.
```
The variable isn't declared in any of the imported `.resource`
files. The test cannot run at all.

**Broker:** never even reached.

**Fix wanted:** declare `${ERROR_TYPE_RESOURCE_NOT_FOUND}` in
`resources/ApiUtils/Common.resource` (the rest of the suite
defines a parallel set of `ERROR_TYPE_*` variables there).


## 55. `051_08_01 / 051_08_02 / 051_09_01` — Delete-Core-@context behaviour is spec-undefined

**Hit:** tests call `DELETE /jsonldContexts/{core-context-id}`
and assert specific status codes (variously 204 / 404 / 400).
The broker treats the core context as immutable and rejects
with 400 ("core context cannot be deleted").

**Spec:** § 6.5 — does not define what should happen when a
client tries to delete the core JSON-LD @context. The tests
disagree with each other on which status to expect (204 from
one, 404 from another), confirming the spec gap.

**Broker:** consistent (always 400 with a descriptive detail).

**Fix wanted:** spec needs to mandate one of: 400 (delete
forbidden), 405 (method not allowed on the core resource), or
204 + reload-from-disk. Once chosen, the three tests should
all assert the same code.


## 56. `019_11_01..08 / 021_09_02` — fixture polygon is self-intersecting at vertex A

**Hit:** suite setup creates a Building with
`building-location-polygon.jsonld`, whose polygon is:
```
A = (13.2865906, 52.5648645)
B = (13.2879639, 52.5648645)
C = (13.2797241, 52.4988679)
D = (13.477478,  52.4712703)
E = (13.5049438, 52.5373084)
```

A and B share the same latitude and B is only 0.0014° east of
A. Edge BC drops straight down from B while edge EA closes the
polygon back to A — and in the tiny x-interval [13.2866,
13.2880] those two edges cross (BC dips below EA's near-A end,
then the polygon "wraps" around). The broker's planar self-
intersection check (mirroring MongoDB 2dsphere strictness)
correctly returns 400 at setup time, which cascades the entire
019_11_* suite plus the related polygon test 021_09_02.

**Spec:** § 4.7 — Polygons must be simple (non-self-
intersecting). § 6.5.3 — bad geometry → 400 BadRequestData.

**Broker:** correct.

**Fix wanted:** replace the fixture polygon with one whose
near-A edge has even modest separation in either lat or lon —
e.g. shifting B east by another ~0.005° eliminates the
self-intersection without changing the test's intent.


## 57. `020_17_0[1-3] / 020_18_0[1-3]` — fixture deletes via the current-state endpoint on a temporal-only entity

**Hit:** the test setup is
```robot
Test Setup    Create Temporal Entity
```
which calls `POST /temporal/entities`. The test body then does
```robot
Delete Entity Attributes
   entityId=${temporal_entity_representation_id}
   attributeId=${attr_name}
```
which expands to `DELETE /entities/{id}/attrs/{name}` — i.e.
the current-state endpoint. Then the test retrieves the
temporal representation with `?timeproperty=deletedAt` and
expects to see the deleted attribute with a `deletedAt`
timestamp and `urn:ngsi-ld:null` as the value-marker.

The broker returns 404 on the DELETE because the entity does
not exist in current state — and so no deletion event is
logged into the temporal store. The follow-up GET returns
`{id, type}` with no attributes, so deep-diff reports the
expected attribute as "removed from dictionary".

**Spec:** § 5.7.2.1 (Create Temporal Representation):
> "If the corresponding Entity does not already exist in the
>  current state, the Context Broker shall NOT create the
>  Entity in the current state."

So the test's assumption that `POST /temporal/entities` will
populate current state is contrary to the spec.

**Broker:** correct.

**Fix wanted:** the test should either
- create the entity in both stores (call `Create Entity`
  before `Create Temporal Entity`), or
- use `DELETE /temporal/entities/{id}/attrs/{name}` — the
  temporal-side delete that exists for exactly this purpose
  (§ 5.7.2.4). The `Delete Attribute From Temporal Entity`
  keyword is already defined in
  `resources/ApiUtils/TemporalContextInformationProvision.resource`.

**Note:** 020_17_01 and 020_18_01 (the Property subtests)
occasionally pass in the full suite due to cross-test state
leak — when run in isolation, all six subtests fail with the
same fixture root cause.


## 58. `033_01_02` — fixture leaks the previous test's CSR into the next test's `exclusive`-mode conflict check

**Hit:** test setup for 033_01_02 creates an `exclusive`-mode
CSR with the same entity scope (`Vehicle:A456` + `OffStreetParking`
idPatterns) as 033_01_01's earlier (default `inclusive`) CSR.
The suite has `Suite Teardown    Delete Created Context Source
Registrations` but only cleans up the most-recently-saved
`${registration_id}` variable — 033_01_01's CSR stays in the
broker between tests.

Per § 5.9.2.4: "An exclusive registration shall conflict with
another registration covering the same Entity scope (regardless
of mode)." Two CSRs with overlapping entityInfo, at least one
exclusive → 409 Conflict on the second create.

**Broker:** returns 409 — correct per spec.

**Test:** asserts 201. Fails.

**Verified:** when 033_01_02 is run truly alone (broker DB
wiped, broker restarted), it passes with 201. Failure only
appears once 033_01_01 has populated the broker.

**Fix wanted:** add a `Test Teardown` that deletes the test's
CSR (not just a suite-level teardown for the last one). The
suite has 4 subtests, each creating a separate CSR; only one
gets removed today.


## 59. `047_03_01` — `Check Notification Data Entities` indexes `type` as an array

**Hit:** the helper keyword in `NotificationUtils.resource`:
```robot
FOR    ${registration_information}    IN    @{notification_data_information}
    Append To List    ${notification_data_entities}
    ...    ${registration_information}[entities][0][type][0]
END
```
The expression `[type][0]` takes the first ELEMENT of `type`.
Python string indexing makes `"Building"[0] == "B"`. So when
the broker correctly returns `"type": "Building"` (string),
the test collects `"B"` into the list and the assertion fails
with `Index 0: Building != B`.

**Spec:** § 5.2.8 defines `EntityInfo.type` as "an NGSI-LD
attribute name" — singular, scalar string. § 6.3.5 compaction
returns a single value as a string, not an array.

**Broker:** correct (returns `"type": "Building"`). Verified by
037_05_01 / 037_05_02 (GET /csourceRegistrations) whose
expectation files have `"type": "Vehicle"` as a string and pass.

**Fix wanted:** drop the trailing `[0]` from the assertion:
```robot
Append To List    ${notification_data_entities}
...    ${registration_information}[entities][0][type]
```

**Broker improvements landed alongside this triage:** the CSR-
sub notification now (a) filters `information[]` to only the
entries matching the subscription's entity scope (§ 5.11.7
SHOULD), and (b) compacts using the subscription's @context so
attribute IRIs come back as short names. Both were genuine
broker bugs visible regardless of the assertion typo.


## 60. `008_01_01` — fixture's expected temporal slice matches neither TRoE history nor current-state shape

**Hit:** suite does two consecutive `POST /temporal/entities/`
with the same id:

setup body:
```
speed:     [{val:120, obs:12:03}, {val:80, obs:12:05}]
fuelLevel: [{val:67, obs:12:03}, {val:53, obs:13:05},
            {val:40, obs:14:07, datasetId:"12345-fuel"}]
```
update body:
```
speed:     [{val:121, obs:12:03}, {val:80, obs:12:05},
            {val:100, obs:12:07}]
fuelLevel: [{val:67, obs:12:03}, {val:53, obs:13:05},
            {val:40, obs:14:07}]   ← no datasetId
```
Then `GET /temporal/entities/{id}` and the expectation has
**3 speed + 4 fuelLevel** entries: the update's three speed
instances, and (3 update fuel + 1 setup datasetId-bearing fuel
that wasn't covered by the update).

That matches an implicit dedup-by-(datasetId, observedAt) rule
where the second POST replaces same-key instances and merges
new ones — applied to the temporal layer.

**Spec — two layers, two semantics:**

- **TRoE** (what the endpoint queried by the test serves):
  § 5.6.10 / § 5.6.11 say a second `POST /temporal/entities/`
  on an existing entity ADDS the new instances to the history.
  Strict spec reading: the TRoE rows after both POSTs are the
  union — 5 speed + 6 fuelLevel.
- **Current state**: in our broker, `POST /temporal/entities/`
  on an entity that doesn't already exist in current state
  intentionally skips current-state creation (§ 5.7.2.1),
  so a follow-up `GET /entities/{id}` returns 404 here. A
  broker that DID create the entity in current state would, on
  the second POST, REPLACE the no-datasetId instances at the
  current-state layer (because current state stores at most
  one instance per (attr, datasetId)) — landing at exactly the
  3 speed + 4 fuelLevel shape the test expects. But that
  isn't where the test is looking.

**Broker:** spec-correct on the TRoE layer (appends). Returns
5+6 from the temporal endpoint.

**Verdict:** the test fixture conflates the two layers. The
expected shape is the current-state outcome of the upsert, but
the assertion reads from the TRoE endpoint. Either the
expected file should be the union (5+6) — matching what the
temporal endpoint truly produces per spec — or the test should
GET the current-state endpoint (which then also needs a setup
that puts the entity into current state to begin with, since
`POST /temporal/entities/` against a non-existent current
entity skips creation per § 5.7.2.1).


## 61. `047_16_01 / 047_16_03` — PATCH /csourceSubscriptions has no @context, so the new entity-selector terms expand differently than the CSR's terms

**Hit:**

Setup (in the same suite):
- CSR1 (Vehicle) and CSR2 (Bus) are POSTed with
  `Content-Type: application/ld+json` + an `@context` array
  pointing at the test-suite-compound. The compound's nested
  `test-suite.jsonld` has an explicit mapping
  `"Vehicle" → "https://ngsi-ld-test-suite/context#Vehicle"`,
  so each CSR's `information[0].entities[0].type` is stored
  as `https://ngsi-ld-test-suite/context#Vehicle`.
- The CSR-subscription is initially created the same way
  (entities[].type = `Building`, expanded against
  test-suite-compound to `https://ngsi-ld-test-suite/context#Building`).

Test body:
```robot
PATCH /csourceSubscriptions/{id}    json={"entities":[{"type":"Vehicle"}]}
```
which `requests` sends as `Content-Type: application/json`
with NO Link header. Per § 6.3.5 the broker resolves `Vehicle`
against the core context's `@vocab` because no @context
information arrives with the request. The PATCH ends up
overwriting `entities[0].type` in the cached subscription as
`https://uri.etsi.org/ngsi-ld/default-context/Vehicle`.

Now the cached subscription's entity-selector IRI is
`uri.etsi.org/ngsi-ld/default-context/Vehicle`, but CSR1's
entityInfo IRI is `ngsi-ld-test-suite/context#Vehicle`. Same
token "Vehicle" but expanded under two different @contexts.
They don't compare equal, the sub doesn't match CSR1, and the
post-PATCH `newlyMatching` notification never fires.

**Why 047_16_02 (Bus) accidentally passes:**

`test-suite.jsonld` defines `Vehicle`, `Building`,
`OffStreetParking` and a handful of attribute terms — but NOT
`Bus`. When CSR2 was POSTed, the broker walked the compound
@context looking for `Bus`, didn't find it, and fell through
to the core `@vocab` of `https://uri.etsi.org/ngsi-ld/default-context/`.
So CSR2.entityInfo[0].type was stored as
`https://uri.etsi.org/ngsi-ld/default-context/Bus` — exactly
the same IRI the PATCH later produces for `Bus`.

047_16_01 (Vehicle) hits the asymmetry; 047_16_03 covers both
Vehicle and Bus and fires the notification with only the Bus
half (`data[1]` missing).

**Spec:** broker behaviour is correct for the wire it sees. The
PATCH carries no @context, so `Vehicle` is a locally undefined
term and the broker's resolution against core `@vocab` is the
only spec-aware option.

**Fix wanted:** the test PATCH should send the same @context
that created the resources — either `Content-Type:
application/ld+json` with the `@context` array in the body, or
`Content-Type: application/json` with a Link header naming the
test-suite-compound. Without one of those, `Vehicle` is
locally undefined and the broker has nothing to align it
against.

**Broker improvement idea (future work):** on PATCH of an
existing resource with a stored `_jcResolved`/`jsonldContext`,
fall back to the stored @context if the request omits one.
That would mask this fixture bug, but it changes semantics the
spec doesn't actually mandate, and would also need an opt-out
for clients that intentionally want default-vocab semantics.
Not pursuing for now.


## 62. `020_14_01 / 020_14_02` — "cut at attribute boundary" for temporal pagination is broker-specific

**Hit:** the fixture has 59 `speed` instances on
`2020-01-01T01:01..01:59` and 59 `fuelLevel` instances on
`2020-01-02T01:01..01:59` (the two attributes have
non-overlapping time ranges, hence "unsynchronized").

- **020_14_01** — `timerel=after timeAt=2019-01-01Z`:
  asks for everything. Expects the response `fuelLevel` to be
  **empty** — broker is supposed to truncate at the cap and
  cut at the attribute boundary, so only the first attribute
  (`speed`) survives.
- **020_14_02** — `lastN=100 timerel=before timeAt=2021-01-01Z`:
  asks for the 100 most-recent instances. Expects the response
  `speed` to be **empty** — the same cut-at-boundary, but in
  reverse (lastN sorts descending so fuelLevel is the "first"
  attribute and survives, while speed is cut).

**Broker:** does not cut at the attribute boundary. Returns
instances of BOTH attributes (the prefix that fits under the
configured `--troeInstanceCap`, default 20 per § 6.3.10),
interleaved. So both attributes end up with some instances and
both assertions fail.

**Spec:** § 6.3.10 says when the result exceeds the broker's
configured page size, the response is 206 with a Content-Range
header. The spec is **not explicit** about "cut at attribute
boundary" — that is one valid pagination strategy among
several (uniform per-attribute cut, time-window cut, attribute-
boundary cut, etc.). The ETSI suite encodes the boundary-cut
choice without spec backing.

Same family as doubt #41 (`020_14` is "since v1.5.1" — and the
v1.5.1 wording introduced the cap concept without nailing the
cut algorithm).

**Verdict:** the broker's interleave-within-cap approach is
spec-compliant; the suite's expectation requires a specific
implementation. Either the spec adopts boundary-cut as the
mandated algorithm (and 020_14 gets explicit normative
backing) or the suite needs to assert observably (e.g.
"total instance count ≤ cap" rather than "this attribute is
empty").
