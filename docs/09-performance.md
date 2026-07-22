# Performance Engineering

Performance is a correctness property here, not a nice-to-have: if tracking a field
costs more than the project's stated budget, users will not adopt it regardless of
how good the query/visualization story is (Phase 2's evidence: game/finance
audiences are the target market, and both have hard, unforgiving latency budgets).

## Budget targets (v1, subject to revision once benchmarked)

- Untracked type: **exactly zero overhead** — verified by codegen diff (identical
  assembly with/without unused `chronicle` includes), not just "should be inlined
  away."
- Tracked scalar field write (`operator=` on `tracked<T>`, `RingWindow` retention,
  buffer not full): target **single-digit nanoseconds**, dominated by one relaxed
  atomic increment + one non-atomic write into a pre-allocated per-thread ring slot —
  same order of magnitude as a Tracy zone (~10-20ns per the public Tracy benchmarks
  we're calibrating against, Phase 1).
  no heap allocation.
- Tracked container mutation (`tracked_vector::push_back`): target underlying
  container cost + a small constant (one event record), not a multiple of it.
- Storage Engine drain (cold path): must never block the thread that produced the
  event beyond enqueueing it into the lock-free ring buffer; snapshot/compression
  work happens on a dedicated background thread or between frames (game-engine
  friendly), never synchronously inline with a tracked write.

## Techniques, mapped to where they apply

- **Zero-cost abstractions**: `tracked<T>`'s `operator=` must be trivially inlinable
  down to "write value, enqueue event" with no virtual dispatch — `Trackable`
  adapters are resolved at compile time via concepts/templates (Phase 7), never via
  a runtime vtable, so the compiler can fully inline the hot path.
- **Thread-local ring buffers**: per-thread, lock-free, fixed-capacity — avoids both
  cross-thread contention and any hot-path allocation (direct application of Tracy's
  proven approach, Phase 1/5).
- **Cache locality / columnar encoding**: Storage Engine's columnar layout (Phase 6)
  isn't just a compression win — grouping same-field values together also means
  Replay/Query scans touch fewer cache lines per query than a naive row-major log
  would.
- **Branch prediction friendliness**: the hot-path check ("is this stream currently
  being recorded / is the session active") should be a single predictable branch on
  a value that rarely changes during steady-state execution, not a chain of
  policy lookups per write.
- **SIMD interaction**: explicitly documented cost (Phase 4) — tracked scalar fields
  inside a hot loop forfeit auto-vectorization across those specific writes; the
  mitigation is workload-level, not tooling-level: track the *boundary* values of a
  vectorized loop (before/after), not every intermediate lane, when the loop itself
  is performance-critical. This should be a documented pattern, not a silent trap.
- **Arena/pool allocation for Storage**: snapshot and delta buffers are allocated
  from a dedicated pool/arena per session, not `malloc`/`free` per chunk — keeps
  Storage's own overhead bounded and avoids allocator contention with the
  instrumented program's own allocations.
- **NUMA / false sharing**: per-thread ring buffers must be padded/aligned to cache
  line boundaries (`std::hardware_destructive_interference_size`) so two threads'
  buffers never false-share a cache line — a one-line fix with an outsized penalty if
  missed, called out explicitly so it isn't lost in implementation.
- **Lazy decoding**: Query/Replay never eagerly decompresses or decodes more of a
  session than a query actually touches — a `diff(t0, t1)` over a 10-minute session
  should cost proportional to the events between t0 and t1 plus nearest snapshots,
  never proportional to total session size.

## Measurement discipline

Per the Performance Philosophy principle (measure first, not assumptions): every
budget number above is a **target to validate with a microbenchmark suite**
(`chronicle-bench`, part of the repo from v0.1 per Phase 11), not a claim to publish
unverified. The benchmark suite must include a regression gate in CI (Phase 11) so a
future change that silently reintroduces a heap allocation on the hot path is caught
before merge, not after a user reports a frame-time regression.

## Where we deliberately do NOT optimize prematurely

Compression ratio, on-disk format micro-efficiency, and cold-path query latency are
explicitly **lower priority than hot-path write cost** for v1 — a user will forgive a
slow `diff()` call run once after a bug report; they will not forgive a tracked field
costing measurable frame time in a shipping game loop. This ordering should govern
where profiling/optimization effort goes when the two compete for the same
engineering time.
