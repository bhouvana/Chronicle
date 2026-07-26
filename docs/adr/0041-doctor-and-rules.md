# ADR 0041: `chronicle-cli doctor` and `chronicle::rules`

## Status
Accepted

## Context
Following a review of the full capability surface built across
[docs/13-vision.md](../13-vision.md)'s 10 layers, the recommendation was to
stop adding recording features and start asking "what does the ecosystem
build on top of Chronicle" — with two concrete, low-risk, high-value
starting points named explicitly: (1) a tool that tells you what's wrong
before you know what to ask, and (2) moving from "what happened" to "did
the program violate any expectations" (runtime verification, not just
observability). A proposed general query language/IR was evaluated and
explicitly deferred — see [ADR 0035](0035-live-queries.md)'s existing
"three concrete queries, not a general query language" decision, which
this doesn't reopen; a query engine spanning both the live typed API and
the offline type-erased CLI model is a real, separate, database-engine-scale
project, not a refactor, and better justified by evidence from real usage
of the narrower tools below than designed speculatively.

## Decision

### `chronicle-cli doctor`
Pure composition — zero new detection algorithms beyond what
`narrate.cpp` ([ADR 0038](0038-narrative-composer.md)) already
established. `tools/cli/doctor.cpp` adds two **whole-file** siblings of
narrate's position-bounded passes (a bounded-lookahead race scan across
the entire merged history, a whole-history growth scan per field) —
kept as separate, small implementations rather than forced to share code
with narrate's bounded versions, since the two have genuinely different
scope (whole-file sweep vs. look-backward-from-one-position), the same
tolerance for at-a-boundary duplication this project already accepted for
`replay_indexed`/`replay_keyed`. Reuses `group_by_object`,
`most_changed_streams`, `thread_index`, `merge_entire_session` for
everything else. **Exit code reflects health** (0 = clean, 1 = issues
found), so it's usable as a CI gate, not just an interactive report.

### `chronicle::rules`
`check_rule(history, predicate)` (offline, cold path) and
`watch(field, predicate, callback)` (live) are one concept — the same
predicate, evaluated either after the fact over a `Timeline<T>` or live via
the same composable `Stream<T>::RecordHook` mechanism
[ADR 0040](0040-composable-record-hooks.md) just made possible.
Composability is the point: a `watch()` can now coexist with a `derive()`
binding on the same field, verified directly (not assumed).

**Scoped explicitly**: scalar `tracked<T>` and point predicates only
("is this value currently valid"). Container rules
("`inventory.size() <= 128`") and temporal predicates ("never decreases
for 30s") are real, named future scope — the latter needs a genuine
windowed evaluator, materially more machinery than a point check. No
`CHRONICLE_RULE(expr)` macro: turning an arbitrary boolean expression into
a reactively-re-evaluated predicate is a separate, Catch2-expression-
decomposition-scale undertaking; this ships the mechanism such a macro
could be layered on top of, not the macro itself.

### Verification performed
`doctor`: run against a real, deliberately provocative file (an 8-item
never-shrunk vector past the leak threshold, two real racing threads) —
correctly reports 2 races and 1 leak with exit code 1; a clean file
correctly reports "OK, no issues found" with exit code 0, including
correct persisted-provenance/derivation counts pulled from format v5
([ADR 0039](0039-persist-provenance-and-derivation.md)).

`rules`: `check_rule` correctly finds every real violation in a recorded
history and nothing when none exist; `watch` fires live on a real
violation and stays silent when the value is valid; destroying the watch
handle stops the callback (verified, not assumed); a `watch` and a
`derive()` binding on the same dependency field both fire correctly on
one mutation.

Full suite: **446/446 checks across 115 tests** (105 before this ADR's
two features).

## Consequences
- Positive: real, working answers to two of the highest-value items named
  in the review — "tell me what's wrong" and "verify expectations, not
  just observe" — both built entirely from already-existing, already-
  verified primitives or extension points.
- Positive: `doctor`'s exit code makes it a real CI gate, not just a
  human-readable report — a first, concrete step toward "other tooling
  builds on Chronicle."
- Negative: container/temporal rules remain unbuilt — real future scope,
  not silently out of reach, just not attempted speculatively ahead of a
  concrete need.
- Negative: the general query language proposal remains deferred, now for
  the second time (first in ADR 0035) — still the right call absent new
  evidence that the three-plus-`narrate`-plus-`doctor` query surface is
  insufficient in practice.
