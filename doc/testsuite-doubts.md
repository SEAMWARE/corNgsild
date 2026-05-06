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
