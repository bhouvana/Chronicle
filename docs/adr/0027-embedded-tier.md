# ADR 0027: Fixed-Capacity Allocation-Free Tier for Embedded Targets

## Status
Accepted

## Context
[docs/02-competitive-gap-analysis.md](../02-competitive-gap-analysis.md)
concluded "mostly no" for constrained embedded targets (flash/RAM budgets,
no heap for a history buffer) but flagged higher-end automotive/industrial
ECUs as plausible "if there's a tiny, allocation-free tier."
[docs/12-future-research-topics.md](../12-future-research-topics.md) topic 7
asks whether such a tier can exist "without compromising the core model's
generality for the primary game/sim/finance audience."

`chronicle::Session`/`Stream<T>` fundamentally use `std::vector`,
`std::unique_ptr`, and `std::mutex` — necessary for that primary audience's
concurrency and unbounded-retention needs, but exactly the machinery an
allocation-free embedded tier can't carry. Retrofitting them out from
underneath the existing API would be the "compromise" this topic warns
against.

## Decision
`include/chronicle/embedded.hpp`: `chronicle::embedded::TrackedScalar<T, Capacity>`,
a genuinely separate, additive module — not reachable from
`#include <chronicle/chronicle.hpp>`, same "opt-in module a caller reaches
for by name" shape as the Tracy bridge. A fixed-capacity circular buffer
over `std::array<T, Capacity>` (stack or static storage, `Capacity` fixed at
compile time), tracking `total_recorded()` (a fixed-width counter, the
allocation-free counterpart to `Stream<T>::current_version()`) and
oldest-to-newest indexed access over whatever history hasn't been evicted.

**Deliberately not thread-safe**: no atomics, no mutex, no per-thread ring
registry. The audience this targets (a single-core or cooperatively
scheduled ECU task) doesn't need `Stream<T>`'s concurrency story, and
paying for it here would reintroduce the exact hidden cost
[09-performance.md](../09-performance.md)'s zero-cost philosophy exists to
avoid. A caller with genuinely concurrent producers must synchronize
externally; this class does not attempt to provide it.

### Verification performed
`tests/unit/embedded_test.cpp` (4 tests): current-value read-back and
implicit conversion; correct indexing and `total_recorded()`/`size()`
before the buffer wraps; correct oldest-value eviction after wrapping
(capacity 3, 5 total writes, confirms exactly the newest 3 survive in
order); and the allocation-free claim itself, verified directly by
overriding global `operator new`/`operator delete` with a counter and
running 1000 mutations through a `TrackedScalar<double, 64>`, not assumed
from reading the code.

**A real, non-obvious bug found while writing that last test, worth
recording**: the first version compared `g_heap_alloc_count` directly
inside the `CHRONICLE_CHECK(...)` macro call and failed even though a
`fprintf`-based debug print immediately beforehand showed the count
hadn't moved. Root cause: `CHRONICLE_CHECK` calls `record_check(bool, std::string const&, std::string const&, std::string const&, int)`,
and passing `__func__`/`#expr`/`__FILE__` (all longer than
`std::string`'s SSO buffer) implicitly constructs heap-allocating
temporaries as call arguments — on *every* `CHRONICLE_CHECK`, whether it
passes or fails. C++ doesn't sequence one argument's construction
relative to another's, so the diagnostic-string allocation could happen
before the checked expression itself was evaluated, making the check
observe its own bookkeeping overhead rather than the code under test. Fixed
by snapshotting the comparison into a plain `bool` local *before* the
`CHRONICLE_CHECK` line, so the macro only ever sees an already-fixed value.
This is a real hazard for any future "assert zero allocations" test written
against this test framework, not specific to this feature — worth knowing
before writing another one.

## Consequences
- Positive: a real, working answer to topic 7's question — yes, a tiny
  allocation-free tier is possible, as a genuinely separate module, with no
  changes to the existing `Session`/`Stream<T>` machinery the primary
  audience depends on.
- Positive: found and fixed a real, reusable gotcha in this project's own
  test framework (allocation-counting tests must snapshot before
  `CHRONICLE_CHECK`, not compare live inside it).
- Negative: no thread safety, no on-disk format integration, no call-site
  capture, no HLC — a deliberately minimal tier, not a feature-parity
  embedded `Stream<T>`.
- Negative: oldest history is silently evicted once `total_recorded()`
  exceeds `Capacity`, with no unbounded/on-disk escape hatch — the fixed-
  footprint trade this tier exists to make, not an oversight.
