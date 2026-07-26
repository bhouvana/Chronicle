# ADR 0033: Derived State via an Explicit Reactive API

## Status
Accepted

## Context
[docs/13-vision.md](../13-vision.md)'s Layer 4 (`gold = income - tax`,
auto-explained) explicitly named a fork and declined to scope it until one
side was chosen: (a) an explicit reactive/signals API the caller opts
fields into, or (b) inferring dependencies from source via static
analysis.

**Decision, made now: (a).** Option (b) would mean parsing arbitrary C++
expressions and building a real data-flow graph from source — a project on
the scale of (or larger than) `tools/codegen`'s Clang-LibTooling tool
([ADR 0012](0012-chronicle-codegen-libtooling.md)), genuinely
research-shaped, not an incremental step. Option (a) is buildable directly
on an extension point this project already has and already uses for
exactly this shape of problem.

## Decision
`include/chronicle/derived.hpp`: `chronicle::derive(target, compute, dep1, dep2, ...)`
returns an owning `std::unique_ptr<derived::Derivation<Result, Deps...>>`.
Construction attaches one `Stream<T>::RecordHook`
([stream.hpp](../../include/chronicle/stream.hpp), the same extension
point the Tracy bridge, [ADR 0013](0013-tracy-bridge.md), already proves
out for a different consumer) per dependency; whenever any dependency
mutates, its hook fires synchronously, `recompute()` calls the caller's
compute function, diffs the new dependency values against the last-seen
ones, writes the new result into `target` via plain assignment (so
`target` remains an ordinary `tracked<Result>` — `history()`/`snapshot()`
work on it unmodified), and stores a `DependencyChange` explanation
(dependency name via `dep.stream()->name()`, old/new rendered values,
changed flag) in `chronicle::derived::Registry`, keyed by
`(target_stream->id(), version)` — the third reuse this cycle of the same
`(stream_id, version)`-keyed-registry shape
(`chronicle::interposition::Registry`, ADR 0029;
`chronicle::provenance::Registry`, ADR 0032), all made address-reuse-safe
by `StreamBase::id()` (ADR 0032).

`chronicle::explain(target, version)` retrieves the explanation —
"gold changed because income increased, tax stayed constant" is exactly
`for (auto const& c : *explain(gold)) print(c.name, c.old_value, "->", c.new_value, c.changed)`.

**Safety**: `Derivation`'s destructor detaches every hook
(`set_record_hook(nullptr, nullptr)`) before the object goes away — the
hook trampoline holds a raw `this` pointer, and `stream.hpp`'s own
`RecordHook` doc comment already states the exact discipline this must
follow ("detach strictly after any concurrent producer activity"). Not
following this would leave a dangling-pointer hook active on a
still-mutating stream.

**Explicit scope, stated up front, not discovered mid-build:**
- `RecordHook` is single-slot per stream (`stream.hpp`'s own comment). A
  dependency field that already has a Tracy-bridge hook attached cannot
  simultaneously feed a `Derivation` in this increment — real, documented,
  not silently overwritten.
- No derived-of-derived: dependencies must be plain `tracked<T>` fields,
  not another `Derivation`'s target — avoids real, harder problems (cycle
  detection, recomputation ordering across a dependency graph) genuinely
  out of scope here.
- In-process only, same as ADR 0032 — not persisted to the `.chronicle`
  wire format.

### Verification performed
`tests/unit/derived_test.cpp` (4 tests), using the vision doc's own
example (`gold = income - tax`): mutating `income` correctly recomputes
`gold` and `explain()` correctly attributes the change to `income` (not
`tax`); mutating `tax` instead correctly flips the attribution; `gold`'s
own `history()` shows all three real recorded values (`track()`'s initial
0, then two recomputations) — proving it behaves as an ordinary tracked
field, not a special case; and destroying the `Derivation` handle and then
mutating a dependency again confirms recomputation genuinely stops — the
concrete verification that the destructor's hook-detach actually works,
not just compiles. Full suite: **381/381 checks across 97 tests.**

## Consequences
- Positive: a real, working answer to Layer 4's chosen fork, built with
  zero new recording mechanism — reuses `RecordHook` exactly as designed.
- Positive: `target` stays a first-class `tracked<Result>` throughout —
  every existing query (`history`, `snapshot`, `diff`, `last_writer`,
  even `possible_race`/`range_anomalies` from earlier this cycle) works on
  a derived field without any special-casing.
- Negative: the single-slot `RecordHook` conflict means `derive()` and the
  Tracy bridge can't both watch the same dependency field today —
  composable multi-hook dispatch is real, separate future scope.
- Negative: no derived-of-derived — a real limitation for genuinely
  chained computations (`c = f(b)`, `b = g(a)`), left for whenever
  dependency-graph ordering is worth the real complexity it needs.
- Negative: in-process only, like every other Layer 3/4 feature shipped
  this cycle — persistence across all of them is a shared, real, future
  increment, not three separate ones.
