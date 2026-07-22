# ADR 0004: v0.1 `Stream<T>` Uses a Mutex-Protected Staging Deque, Not the Target Lock-Free Ring Buffer

## Status
Accepted

## Context
[05-architecture.md](../05-architecture.md) and
[06-recording-model.md](../06-recording-model.md) specify per-thread,
lock-free SPSC ring buffers as the Recording Engine's hot-path mechanism, and
[09-performance.md](../09-performance.md) commits to single-digit-nanosecond
tracked writes on that basis. [RFC 0001](../rfc/0001-core-recording-and-instrumentation-model.md)
initially proposed implementing that design directly for v0.1.

While implementing `chronicle-core`'s first working slice, it became clear
that a provably correct lock-free SPSC ring buffer (right memory ordering,
correct behavior under overflow, cache-line padding against false sharing) is
itself a nontrivial piece of engineering that [09-performance.md](../09-performance.md)'s
own "measure first" philosophy says should be built against a benchmark
baseline, not before one exists. Blocking v0.1's first working end-to-end
slice — `track()` → mutate → `history()` — on getting that right first would
invert the project's own stated priorities (working, honestly-scoped
milestones over premature optimization, per
[10-roadmap.md](../10-roadmap.md)'s "every milestone must be independently
useful").

## Decision
`Stream<T>` (`include/chronicle/stream.hpp`) stages incoming events in a
single `mutable std::mutex`-protected `std::deque<Event<T>>` per stream
(not per thread). `record()` (the hot path called from `tracked<T>::operator=`)
locks this mutex, applies the configured `OverflowPolicy`, and pushes. Draining
into the durable log happens synchronously and on-demand: any query
(`history()`, `snapshot_at()`) or an explicit `Session::drain_all()` call
moves everything staged into the per-stream log under a second mutex,
avoiding the need for a background thread to exist at all in v0.1.

## Consequences
- Positive: unblocks a real, tested, working v0.1 slice immediately —
  `include/chronicle/`, `tests/unit/`, and `examples/minimal/` all build and
  pass against this implementation today.
- Positive: the public API (`tracked<T>`, `track()`, `history()`,
  `snapshot()`, `diff()`) is unaffected — `Stream<T>`'s internals are exactly
  the boundary the architecture ([05-architecture.md](../05-architecture.md))
  already isolates, so swapping the staging mechanism later requires no
  caller-visible change.
- Negative: v0.1 does **not** meet the nanosecond-scale hot-path budgets in
  [09-performance.md](../09-performance.md) — a mutex lock/unlock per
  `record()` call is real, measurable overhead (typically tens of
  nanoseconds uncontended, much worse under contention). This must not be
  quoted as the project's performance story; `chronicle-bench`
  ([10-roadmap.md](../10-roadmap.md)'s v0.1 scope) needs to measure and
  publish v0.1's actual numbers rather than the target-design numbers.
- Negative: `Block` overflow policy has no real blocking semantics yet
  (see [RFC 0001's Resolution](../rfc/0001-core-recording-and-instrumentation-model.md#resolution))
  — degrades to `DropOldest` until the lock-free buffer lands.
- Follow-on: replacing this with the per-thread lock-free ring buffer from
  the original design is tracked as the concrete next optimization once
  `chronicle-bench` exists to validate it against — not a research question
  (the design is already specified), just not yet built.
