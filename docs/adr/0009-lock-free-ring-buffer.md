# ADR 0009: Lock-Free Per-Thread Ring Buffer Replaces the Mutex-Staging Deque

## Status
Accepted

## Context
[ADR 0004](0004-mutex-staging-deque-for-v01.md) shipped v0.1's `Stream<T>`
against a mutex-protected staging deque instead of the lock-free per-thread
ring buffer [05-architecture.md](../05-architecture.md) and
[06-recording-model.md](../06-recording-model.md) specify, explicitly
deferring the harder design until `chronicle-bench` gave a real baseline to
build against ([bench/RESULTS.md](../../bench/RESULTS.md)). That baseline
now exists. This ADR closes that gap — and records, honestly, that closing
it was considerably harder than the original plan assumed, surfacing two
real bugs that single-threaded testing structurally cannot catch.

## Decision

### Design
`include/chronicle/ring_buffer.hpp`: a classic bounded SPSC circular buffer
(`capacity - 1` usable slots, the standard trick for distinguishing full
from empty via head/tail equality alone), power-of-two sized for cheap
bitmask indexing instead of modulo, cache-line-padded `head_`/`tail_` atomics
to avoid false sharing.

`Stream<T>` (`include/chronicle/stream.hpp`) gives each thread that calls
`record()` its own `RingBuffer<Event<T>>`, looked up via a `thread_local`
cache. The fast path (ring has room) is lock-free for every
`OverflowPolicy`. The slow path differs:
- `DropNewest`: discard, still lock-free.
- `Block`: spin-retry against the same ring, still lock-free — a real
  blocking policy now that a consumer-drained ring exists, closing the gap
  [RFC 0001](../rfc/0001-core-recording-and-instrumentation-model.md) left
  open ("`Block` should regain real meaning... not before" the ring buffer
  existed).
- `DropOldest`: a fully lock-free single-producer overwrite-when-full ring
  needs a seqlock-style per-slot generation scheme to stay race-free under
  concurrent overwrite, which this pass does not attempt — implementing and
  rigorously verifying that (without ThreadSanitizer, see below) was judged
  too high-risk for the value it would add over the chosen alternative:
  `DropOldest`'s overflow case takes a small per-stream `overflow_mutex_` to
  safely evict-then-push, and `drain()` takes that same mutex only when the
  stream's policy is `DropOldest`. Every other policy, and the common case
  (ring has room) for every policy including `DropOldest`, never touches
  this mutex.

### Two real bugs, found by actually testing

**Bug 1 — thread-local cache keyed by object identity.** The first version
cached each thread's ring pointer in a `thread_local
std::unordered_map<Stream<T> const*, RingBuffer<Event<T>>*>`, keyed by the
`Stream<T>`'s own address. This crashed almost immediately — not as a rare
edge case, but reliably, because `Session`/`Stream<T>` are routinely
stack-local (every unit test in this project creates one per test
function), so sequential test functions on the same thread constantly reuse
the same stack address for a *different* `Stream<T>` object. A new stream at
a reused address would hit the cache and resolve to a previous, already-
destroyed stream's dangling `RingBuffer` — a real use-after-free, confirmed
via AddressSanitizer after several failed attempts to reproduce it in
isolated minimal examples (the bug only manifested in the actual multi-file
test binary; see the false trails below). Fixed by keying the cache on a
monotonic `id_` assigned once per `Stream<T>` from a global atomic counter
instead of the object's address — an ID is never reused even when memory
is, so a new stream at an old address always misses the cache and gets a
fresh ring buffer.

*False trails worth recording*: chasing this bug included several isolated
repros (a bare `thread_local` map, an over-aligned-member allocation test,
a polymorphic `StreamBase`-derived template, a two-translation-unit
instantiation test) that all ran clean, and a separate AddressSanitizer
"bad free" that turned out to be specific to the Debug CRT
(`ucrtbased.dll`)/ASan interaction on this toolchain, not a real bug —
confirmed by the same binary running clean under ASan in a Release
configuration. Both were dead ends that cost real time; recorded so a
future debugging session on this codebase recognizes them faster.

**Bug 2 — sorting a batch is not the same as keeping the log sorted.**
`drain_impl()` sorts each newly-collected batch by version before appending
it to `log_`, which every query (`snapshot_at`, `snapshot_at_version`,
`current_version`) depends on being globally version-ordered for its
early-`break` logic to be correct. This is *insufficient* under concurrent
producers: `next_version_` is a single atomic counter, so `fetch_add()`
guarantees every call gets a distinct value, but does **not** guarantee
that a thread which received a smaller version number pushes it to its own
ring before a thread holding a larger number pushes to *its* ring — OS
scheduling between "get a version" and "make it visible" can reorder
arrival across threads. A later `drain_impl()` call can therefore collect a
smaller version number that simply hadn't arrived during an earlier drain,
and appending that batch to the end of an already-drained `log_` breaks
sort order permanently. This was caught by
`tests/unit/concurrency_test.cpp` — a genuine multi-threaded stress test —
failing its `sorted` invariant roughly one run in three; it is invisible to
any single-threaded test by construction, since the bug is entirely about
inter-thread arrival ordering. Fixed by merging each new sorted batch into
the existing sorted `log_` via `std::inplace_merge` instead of appending —
verified clean across 40 consecutive runs after the fix (previously ~1-in-3
failure), plus 5 clean runs under AddressSanitizer.

### Verification performed
- `tests/unit/concurrency_test.cpp`: two genuine multi-threaded stress
  tests (8 producer threads × 5,000 events, one under `Block` with a
  concurrent drainer verifying zero data loss and full internal
  consistency, one under `DropOldest` with no concurrent drainer verifying
  no corruption/duplication under heavy, deliberately-forced overflow).
- Full suite (155 checks / 25 tests) run 40 consecutive times with zero
  failures after the fix.
- AddressSanitizer (Release configuration — the Debug-CRT configuration has
  the unrelated toolchain artifact noted above) run 5 times clean.
- **ThreadSanitizer was not available**: this toolchain (Clang targeting
  `x86_64-unknown-windows-msvc`) rejects `-fsanitize=thread` outright
  ("unsupported option... for target"). This is a real gap, not a
  formality — TSan would likely have caught both bugs above faster and
  would catch classes of bug ASan and stress-testing alone cannot guarantee
  finding (the stress tests are probabilistic, not exhaustive). Worth
  revisiting on Linux/macOS CI runners (docs/11's planned CI matrix) where
  TSan is available, before trusting this design under load patterns very
  different from the ones tested here.

### Performance, measured honestly (bench/RESULTS.md)
Single-threaded tracked assignment got **~10ns/op slower** (55ns vs.
ADR 0004's ~43-47ns) — the `thread_local` `std::unordered_map` lookup in
`ring_for_current_thread()` has real cost that apparently exceeds an
uncontended mutex lock/unlock on this platform. The design's actual value
shows up under real concurrency: aggregate throughput across 8 producer
threads (~31ns/op) beats even the old single-threaded mutex number, which a
lock-based design structurally cannot do (every thread serializes through
one lock). Publishing only the flattering number would have repeated the
exact mistake this project already caught itself making in the 0.00ns/op
benchmark artifact and ADR 0007's timestamp-tie assumption — so both numbers
are in `bench/RESULTS.md`, not just the one that looks good.

## Consequences
- Positive: `Block` and `DropNewest` are now fully lock-free on every path;
  `DropOldest` is lock-free except its (rare, by design) overflow case.
  Real concurrent throughput improves with thread count, unlike the mutex
  design.
- Negative: single-threaded/uncontended workloads get measurably slower.
  Not chased further in this pass — worth a follow-up if a program's actual
  usage pattern is single-threaded-dominant and the regression matters in
  practice, but not before it's shown to matter, per this project's own
  "measure before optimizing" rule.
- Negative: `DropOldest` is not fully lock-free — a known, documented,
  deliberate scope decision (a correct lock-free overwrite ring needs a
  seqlock-style design this pass didn't attempt), not an oversight.
- Negative: no ThreadSanitizer coverage on this toolchain. The stress tests
  and ASan give real confidence but are not a substitute for it — flagged
  explicitly rather than implied to be equivalent.
- Follow-on: a thread_local cache lookup cheaper than
  `std::unordered_map::find` (e.g. a small linear-probed array, given the
  number of distinct streams a single thread typically touches is small) is
  a plausible next optimization once/if the single-threaded regression is
  shown to matter for a real use case.
