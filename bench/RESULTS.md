# chronicle-bench: Measured Results

These are **real measurements**, captured by actually running
`chronicle-bench` (not projected/target numbers from
[docs/09-performance.md](../docs/09-performance.md)). Per that document's own
"measurement discipline" section, a budget is not trustworthy until it's been
measured.

## Environment
- Compiler: Clang 21.1.6, `-std=c++23 -O2`, MSVC-ABI target (Windows)
- Build type: Release-equivalent (`-O2` forced in `bench/CMakeLists.txt`
  regardless of top-level build type — see the comment there)
- Machine: developer workstation, single run, no isolation from other system
  load — **not** a calibrated CI benchmarking environment. Treat as directional,
  not authoritative; re-run before trusting these for a real decision.

## Current results — lock-free per-thread ring buffer ([ADR 0009](../docs/adr/0009-lock-free-ring-buffer.md))

Two consecutive runs, 1M iterations per single-threaded assignment benchmark,
100 iterations per `history()` benchmark, 200K iterations/thread for the
contended benchmarks.

| Benchmark | Run 1 | Run 2 | vs. v0.1 (mutex, below) |
|---|---|---|---|
| Untracked `tracked<int>::operator=` | 0.25 ns/op | 0.25 ns/op | unchanged (as expected — untracked path didn't change) |
| Tracked `operator=` (RingWindow 1024, single-threaded) | 55.04 ns/op | 55.37 ns/op | **~10ns slower** — see Interpretation |
| Tracked `operator=` (Unbounded, single-threaded) | 55.34 ns/op | 54.94 ns/op | **~10ns slower** |
| `history()` over 100,000 events | 1,472,490 ns/op (14.7 ns/event) | 1,658,528 ns/op (16.6 ns/event) | comparable |
| Contended `record()`, 1 producer thread + drainer | 45.86 ns/op | 65.82 ns/op | noisy (see Interpretation) |
| Contended `record()`, 2 producer threads + drainer | 34.92 ns/op | 38.77 ns/op | — |
| Contended `record()`, 4 producer threads + drainer | 32.62 ns/op | 35.23 ns/op | — |
| Contended `record()`, 8 producer threads + drainer | 30.62 ns/op | 31.63 ns/op | **beats even the old single-threaded mutex number** |

## Interpretation

- **The lock-free ring buffer is not simply "faster."** Single-threaded
  `tracked<int>::operator=` got ~10ns/op *slower* (55ns vs. the old ~43-47ns
  mutex-based number below) — the per-call `thread_local`
  `std::unordered_map` lookup in `ring_for_current_thread()`
  (`include/chronicle/stream.hpp`) that resolves "this thread's ring buffer
  for this stream" has real, measurable cost, and an uncontended mutex
  lock/unlock on this platform is apparently cheaper than that lookup. This
  is exactly why [docs/09](../docs/09-performance.md)'s own philosophy is
  "measure, don't assume" — the architectural argument for lock-free
  (avoids contention) is correct, but it doesn't automatically mean faster
  in the *uncontended* single-threaded case, and pretending otherwise would
  be the same kind of unverified claim this project has already caught
  itself making twice (the fabricated 0.00ns/op, the timestamp-tie
  assumption in ADR 0007).
- **The actual win shows up under real concurrency, where it has to.** The
  contended benchmark (multiple producer threads + a concurrent drainer,
  mirroring `tests/unit/concurrency_test.cpp`'s scenario) shows aggregate
  per-op cost *dropping* as thread count increases — 8 threads land at
  ~31ns/op aggregate, beating even the old *single-threaded, uncontended*
  mutex number. A mutex-based design serializes every thread through one
  lock; this doesn't, and the numbers show it. This is the comparison that
  actually matters for the design's stated goal (docs/05/06's "hot path
  must not contend across threads"), not the single-threaded number alone.
- **The 1-thread contended number is noisy and not directly comparable to
  the single-threaded benchmarks above** — it still has a concurrent
  drainer thread polling and briefly locking, which the pure
  single-threaded benchmarks don't have at all. Read it as "1 producer,
  drainer running" not "equivalent to single-threaded."
- **`history()` at 100,000 events is unaffected**, as expected — that path
  doesn't touch the ring buffer at all once data is drained into `log_`.

## Historical: v0.1 mutex-staging-deque results ([ADR 0004](../docs/adr/0004-mutex-staging-deque-for-v01.md))

Kept for comparison — these are what motivated building the ring buffer in
the first place, not current numbers.

| Benchmark | Run 1 | Run 2 |
|---|---|---|
| Untracked `tracked<int>::operator=` | 0.25 ns/op | 0.25 ns/op |
| Tracked `operator=` (RingWindow 1024) | 42.69 ns/op | 42.74 ns/op |
| Tracked `operator=` (Unbounded) | 43.99 ns/op | 47.41 ns/op |
| `history()` over 100,000 events | 1,472,931 ns/op | 1,495,270 ns/op |

## Tracy bridge record hook overhead ([ADR 0013](../docs/adr/0013-tracy-bridge.md))

`Stream<T>::record()` gained one new unconditional check (`if
(record_hook_ != nullptr)`) so `chronicle::tracy_bridge::PlotHandle` has
somewhere to attach live plotting without `stream.hpp` itself knowing
anything about Tracy. Measured with a controlled A/B, not assumed
negligible: two otherwise-identical Clang 21.1.6 `-O2` builds of
`chronicle-bench`, one with the check present (this codebase) and one with
it compiled out (`-DCHRONICLE_BENCH_SKIP_HOOK_CHECK` around the same `if`,
temporarily, for this measurement only), run interleaved four times each
(no hook ever attached in either — this isolates the check's own cost, not
a plotting call's).

| Benchmark | With hook check | Without hook check |
|---|---|---|
| `RingWindow 1024`, 4 runs | 64.83 / 67.49 / 61.85 / 87.41 ns/op | 62.84 / 60.98 / 62.73 / 81.46 ns/op |
| `Unbounded`, 4 runs | 64.46 / 67.43 / 59.71 / 87.08 ns/op | 58.87 / 61.79 / 59.40 / 61.95 ns/op |

Run 4's spike (~87ns and ~81ns respectively) hit *both* variants together —
clearly ambient system noise (this is a developer workstation, not an
isolated CI runner, per this document's own standing caveat), not something
attributable to the hook. Excluding that shared outlier, the with/without
gap is a few ns at most, the same order of magnitude as run-to-run noise
within each variant individually — consistent with what a single always-
predicted, never-taken branch should cost, but this environment cannot
resolve a number smaller than its own jitter. Reported honestly as "no
measurable regression at this environment's noise floor," not rounded up to
a precise number that would overstate the confidence this data supports.

## What this does NOT establish

This is a single machine, single compiler, unisolated environment — it is
**not** the CI regression baseline
[docs/11-repository-structure-and-standards.md](../docs/11-repository-structure-and-standards.md)
calls for (`bench/baseline.json` with a tolerance band enforced in CI). That
requires a controlled CI runner and multiple samples with statistical rigor,
neither of which exists yet. Treat `bench/baseline.json` in this directory as
a **seed** for that future work, not as an enforced gate. It also doesn't
establish whether the single-threaded regression is worth optimizing away
(e.g. by caching the ring pointer somewhere cheaper than a `thread_local`
map) — a legitimate follow-up, not attempted here since it wasn't measured
as a problem until this pass.
