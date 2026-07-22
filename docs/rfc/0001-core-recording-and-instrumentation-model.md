# RFC 0001: Core Recording & Instrumentation Model for v0.1

## Status
Accepted, with one implementation deviation from the original proposal below —
see [Implementation Note (v0.1)](#implementation-note-v01) and
[ADR 0004](../adr/0004-mutex-staging-deque-for-v01.md). `chronicle-core` v0.1
now exists at `include/chronicle/` and `src/`, builds and passes its unit
tests (`tests/unit/`) and a working example (`examples/minimal/`).

## Summary
Defines the concrete, implementable slice of the architecture
([05-architecture.md](../05-architecture.md)), recording model
([06-recording-model.md](../06-recording-model.md)), and API
([07-api-design.md](../07-api-design.md)) documents that constitutes v0.1
(scalar-field tracking, in-memory only, manual registration — see
[10-roadmap.md](../10-roadmap.md)). Everything below is scoped to what must be true
for `chronicle::track(int_field, session, "name")` →
`chronicle::history(int_field)` to work correctly, cheaply, and portably.

## Motivation
The architecture/model/API documents establish direction but leave many
implementation-level questions open. This RFC forces those questions to concrete
answers before code is written, per the "no implementation before research
concludes" mandate.

## Detailed design

### `tracked<T>` (scalar wrapper)
```cpp
template <typename T>
class tracked {
public:
    tracked() = default;
    explicit tracked(T initial);

    tracked& operator=(T value) noexcept;   // hot path: see below
    operator T const&() const noexcept;      // implicit read

private:
    T value_;
    StreamHandle stream_{};   // set by track(), default = "untracked, no-op"
};
```
- `operator=` when `stream_` is default (untracked): pure assignment, no branch on
  a "tracking enabled" flag beyond what the optimizer can prove is always-false and
  eliminate — this is the concrete mechanism behind the "zero cost when untracked"
  claim in Phase 7 and must be verified by a codegen-diff test in `chronicle-bench`.
- `operator=` when tracked: write `value_`, then enqueue `{stream_, new_value,
  timestamp, thread_seq, call_site}` into the calling thread's ring buffer. No lock,
  no allocation, no virtual call — `StreamHandle` is a plain struct of small
  integers (index into a per-session table), not a pointer to a polymorphic base.

### Ring buffer (Recording Engine)
- Fixed-capacity, per-thread, single-producer/single-consumer (producer = the
  instrumented thread, consumer = the Storage Engine drain thread) lock-free queue.
- Capacity is set per-session at construction (`Session` config), not globally, so a
  finance order-book session and a small test program don't share a one-size budget.
- Overflow policy (`DropOldest | DropNewest | Block`) is a `Session` config value;
  default is `DropOldest` for `RingWindow` retention (matches "last N seconds"
  semantics naturally) and `Block` for `Unbounded` retention (never silently lose
  data the user asked to keep everything of — surfaces backpressure instead).

### Storage Engine drain (v0.1 scope)
- Single background thread per `Session` (not per stream) draining all of that
  session's ring buffers, applying the snapshot/event-log model from ADR 0002.
- v0.1 snapshot interval default: every 64 events per stream (chosen as a starting
  point for `chronicle-bench` to validate/tune against Phase 9's targets — not
  asserted as final).
- v0.1 storage backend: in-memory only (`Unbounded` capped by process memory,
  `RingWindow` self-bounding by construction) — on-disk format is out of scope for
  this RFC (targeted for the v0.2 RFC per the roadmap).

### Query API (v0.1 scope)
- `history(tracked<T> const&)` returns a `Timeline<T>` that lazily walks the
  stream's snapshots+events on the calling (query-side, cold-path) thread —
  never touches the ring buffer directly, always goes through Storage's
  already-drained representation, so a query never races the hot path.
- `snapshot(tracked<T> const&, TimePoint)` and `diff(Snapshot<T>, Snapshot<T>)` per
  Phase 7's sketch, implemented directly in terms of `Timeline<T>` reconstruction.

## Implementation Note (v0.1)

The design above specifies a lock-free, per-thread SPSC ring buffer feeding a
background drain thread. `chronicle-core` v0.1 as implemented instead uses a
single mutex-protected `std::deque<Event<T>>` per stream as the pending
staging area, drained synchronously (on-demand by any query, or explicitly via
`Session::drain_all()`) rather than by a dedicated background thread.

This is a **deliberate, temporary correctness-first substitution**, not a
silent scope-narrowing: getting a lock-free SPSC ring buffer provably correct
(memory ordering, overflow-under-contention, false sharing) is real,
benchmark-driven work that this project's own performance philosophy
([09-performance.md](../09-performance.md): "measure first, not assumptions")
says shouldn't be attempted before a baseline exists to optimize against. The
public API (`tracked<T>`, `track()`, `history()`) is unaffected by this
substitution — `Stream<T>`'s internals are the only thing that changes when
the lock-free version replaces the mutex-protected one, per the architecture's
own dependency/boundary rules ([05-architecture.md](../05-architecture.md)).
See [ADR 0004](../adr/0004-mutex-staging-deque-for-v01.md) for the full
record of this decision.

## Alternatives considered
- **Global tracking-enabled flag checked in `operator=`**: rejected — even a
  well-predicted branch is a cost the "zero when untracked" claim shouldn't need to
  pay; template/type-level opt-in (untracked `T` vs `tracked<T>`) achieves true zero
  cost instead.
- **Single global ring buffer instead of per-thread**: rejected — reintroduces
  cross-thread contention on every write, the exact cost Tracy's design (Phase 1)
  demonstrates is avoidable.

## Open questions for review (resolved — see Resolution)
1. Is a 64-event default snapshot interval reasonable, or should v0.1 ship without a
   default and require explicit configuration until `chronicle-bench` data exists?
2. Should `Block` really be the `Unbounded` default, given it risks stalling the
   instrumented thread under sustained high write rate — or should v0.1 require the
   user to explicitly acknowledge that tradeoff at `Session` construction?

## Resolution

**Q1 — snapshot interval default: keep 64, but note it is currently inert.**
Because v0.1 scalar `Event<T>` payloads already carry the full new value (see
[06-recording-model.md](../06-recording-model.md) and the note at the top of
`stream.hpp`), there is nothing yet for a "snapshot interval" to do — every
event already reconstructs in O(1). `Session::Config::snapshot_interval`
ships as a field (default `64`) so the `Config` shape is stable for callers
now, but `Stream<T>` does not read it until delta-encoded payloads
(containers, v0.2+) make periodic snapshotting load-bearing per ADR 0002.
Revisit with real data once `chronicle-bench` exists and container adapters
land — do not tune this value against scalar-only workloads, since it isn't
wired to anything they exercise.

**Q2 — `Unbounded` + `Block` default: rejected in favor of a single explicit
default (`DropOldest`) for both retention modes, with `Block` demoted to a
"not yet implemented, degrades to `DropOldest`" state.** True blocking
backpressure requires the lock-free ring buffer's producer to be able to
observe consumer progress without a lock — meaningless to half-implement
against a mutex-protected deque (a "blocking" mutex-based queue is just a
slower `DropOldest`+wait, which is worse than either real semantics). Rather
than ship a `Block` that quietly doesn't block, `Stream<T>::record()`
documents this explicitly (`stream.hpp`, "degrades to DropOldest") and picks
`DropOldest` as the one real, honest default for both retention modes.
Revisit when the lock-free ring buffer (this RFC's original design) is
implemented — `Block` should regain real meaning then, not before.

Both resolutions favor **shipping an honest, working v0.1 over a
partially-real implementation of the fuller design** — consistent with this
project's own standard of never overclaiming what the current implementation
actually does (see [04-technical-limitations.md](../04-technical-limitations.md)'s
closing stance on honest capability claims, applied here to our own internals
rather than to C++ language limits).
