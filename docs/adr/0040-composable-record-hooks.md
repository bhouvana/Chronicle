# ADR 0040: Composable Multi-Hook Dispatch on `Stream<T>::RecordHook`

## Status
Accepted — supersedes [ADR 0013](0013-tracy-bridge.md)'s "only one hook at
a time is supported deliberately" framing (ADR 0013 itself is not edited,
per this project's standing convention of superseding rather than
rewriting).

## Context
[docs/13-vision.md](../13-vision.md)'s remaining-work list put this at the
top once persistence shipped ([ADR 0039](0039-persist-provenance-and-derivation.md)).
The real problem: `Stream<T>::RecordHook` was single-slot by original
design, and two real, independent consumers already existed and already
conflicted — `chronicle::tracy_bridge::PlotHandle` ([ADR 0013](0013-tracy-bridge.md))
and `chronicle::derived::Derivation` ([ADR 0033](0033-derived-state.md),
whose own doc comment stated this exact limitation explicitly). A field
couldn't be both Tracy-plotted and a `derive()` dependency.

**ADR 0018 constraint**: `Stream<T>` is reachable from
`#include <chronicle/chronicle.hpp>`, so `set_record_hook()`'s existing
signature is under the v1.0 API stability commitment. This had to ship as
a purely additive change — no existing signature touched.

## Decision
`Stream<T>` gains `kMaxRecordHooks = 4` fixed slots (two known real
consumers plus real headroom — a documented, mechanically extensible
limit, not a fundamental one, same framing as [ADR 0011](0011-tracked-type-explicit-handle.md)'s
8-field cap), each `{RecordHook fn, void* context}`. A fixed array, not a
`std::vector`: an unbounded/heap-allocating list would reintroduce the
allocation cost this extension point was built specifically to avoid.

Two new, additive methods: `add_record_hook(hook, context) -> std::size_t`
(installs into the first free slot, returns a handle; throws
`std::runtime_error` if all 4 slots are full — a cold, rare path, same
"exception is the right tool off the hot path" convention
[session_writer.hpp](../../include/chronicle/io/session_writer.hpp)
already uses) and `remove_record_hook(std::size_t handle)`.
`set_record_hook(RecordHook, void*)` **keeps its exact old signature**,
reimplemented as sugar: clears every slot, installs `hook` into slot 0 if
non-null. `set_record_hook(nullptr, nullptr)` still means "detach
everything," exactly as before — any code built against the old API
keeps working completely unchanged, verified directly (see below), not
assumed.

`record()`'s hot-path dispatch became a fixed 4-iteration loop over slots
(each still a simple null-check), replacing the single
`if (record_hook_ != nullptr)`. Same non-synchronized-with-`record()`
discipline as before applies per-slot: attach/detach must happen before/
after any concurrent producer activity on the stream, not concurrently
with it.

Both real consumers were migrated to the new API: `chronicle::derived::Derivation`
now stores one handle per dependency (`hook_handles_`), so its destructor
removes exactly its own slots, never whatever else is attached.
`chronicle::tracy_bridge::PlotHandle` stores one handle, correctly
re-registering under the new object's address (not the old, now-stale
one) on move.

### Verification performed
**Cost, measured not assumed** (this project's standing bar for anything
hot-path-adjacent, set by ADR 0013 itself): re-ran `chronicle-bench`'s
existing `tracked_assignment_ring_window_1024_single_threaded` benchmark
(the no-hooks-attached case) post-change — **66.58 ns/op**, against
63.32 ns/op from the last real pre-change run on this same machine. A
~5% difference, well inside this project's own already-documented
30-50% run-to-run noise band (`bench/RESULTS.md`) — the 4-slot loop does
not meaningfully regress the common case.

`tests/unit/record_hook_test.cpp` (4 tests): two independently-attached
hooks both fire on one `record()` call; removing one via its handle
leaves only the other firing; `set_record_hook()`'s old single-hook
overwrite behavior works exactly as before (a real backward-compatibility
check); a 5th hook throws.

`tests/unit/derived_test.cpp` gained the actual composability proof: a
manually-attached second hook (standing in for what Tracy would attach)
coexists with a `Derivation`'s own hook on the same dependency stream —
both fire on one mutation, neither displacing the other.

**Tracy bridge, verified live, not just compiled**: built `examples/tracy`
against the real, installed Tracy library (`D:\vcpkg\installed\x64-windows`),
ran it under a real `tracy-capture` process, exported via
`tracy-csvexport -u -p`, and confirmed all **40** real plot points with
the exact expected values (97, 98, 95, 96, 93, ...) — the same
end-to-end methodology ADR 0013 itself originally used, re-run against
the migrated implementation, not assumed to still work because it
compiled.

Full suite: **432/432 checks across 110 tests.**

## Consequences
- Positive: the actual problem is solved — Tracy plotting and `derive()`
  can now share a field's hooks, and any third future consumer has real
  headroom (2 of 4 slots used).
- Positive: zero breaking changes — `set_record_hook()`'s signature and
  behavior are byte-for-byte preserved, verified with a direct test, not
  assumed from reading the diff.
- Positive: real, honest cost measurement shows no meaningful regression
  for the overwhelming common case (no hooks at all).
- Negative: `kMaxRecordHooks = 4` is a real, if generous, ceiling — a 5th
  simultaneous consumer would need this limit raised (a mechanical,
  documented change, same as ADR 0011's field-count cap).
- Negative: `add_record_hook()`'s linear slot search and
  `remove_record_hook()`'s direct index clear are both O(kMaxRecordHooks),
  not O(1) — irrelevant in practice at 4 slots, worth revisiting only if
  the limit is ever raised substantially.
