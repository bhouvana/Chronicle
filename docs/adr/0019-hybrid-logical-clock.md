# ADR 0019: Opt-In Hybrid Logical Clock for Cross-Stream Ordinal Queries

## Status
Accepted

## Context
[10-roadmap.md](../10-roadmap.md)'s v2.0 scope calls for a "hybrid logical
clock upgrade for cross-stream causal ordering (Phase 6's reserved field),
improving on best-effort timestamp correlation." [06-recording-model.md](../06-recording-model.md)
and [ADR 0003](0003-causal-not-global-ordering.md) both describe this as
filling an already-reserved wire-format field. Verified directly, not
assumed: no such field exists anywhere in `event.hpp`, the wire format, or
any ADR's actual implementation notes — a real, previously-undiscovered
gap between the original design sketch and what got built, the same
category of gap this project has already found twice before (the columnar
wire-format sketch in docs/06 that v0.1/v0.2 never implemented,
[ADR 0012](0012-chronicle-codegen-libtooling.md)'s `[[chronicle::track]]`
attribute that Clang silently drops). This is a genuinely new field
(format v3 → v4), not a reserved slot being filled in.

**What an HLC actually buys here, stated honestly**: Chronicle has no
message-passing between streams to piggyback logical-clock updates on the
way distributed-systems HLCs usually do (a thread mutating `Stream<T>` and
a thread mutating `Stream<U>` have no synchronization between them by
design — that absence is the whole point of [ADR 0003](0003-causal-not-global-ordering.md)).
So this cannot and does not solve genuine cross-thread causal ordering
under races — that remains exactly as impossible as ADR 0003 already
established, for the same reason (no synchronization the language gives us
to observe). What it *does* solve: (1) a single, monotonic, tie-free
ordinal comparable **across different `Stream<T>`/`Stream<U>` instances**
sharing a `Session` — something per-stream version counters structurally
cannot provide, since `Stream<T>::current_version()` compared against a
different `Stream<U>`'s means nothing; and (2) the timestamp-tie problem
[ADR 0007](0007-timestamp-ties-under-optimization.md) found for
`snapshot_at()` (`steady_clock` ties on fast back-to-back mutations)
disappears structurally, since the logical counter always advances even
when physical time doesn't.

## Decision
`include/chronicle/hlc.hpp`: `HlcTimestamp{physical_us, logical}` packed
into one lock-free `std::atomic<std::uint64_t>` (44 bits microseconds since
the owning `Session`'s `start()`, 20 bits logical tie-breaker) —
deliberately not a wider atomic struct requiring 128-bit CAS whose
lock-free-ness would need re-verifying per platform, and deliberately not a
mutex: this project already spent real effort removing one lock from the
hot path once ([ADR 0009](0009-lock-free-ring-buffer.md)) and isn't
reintroducing one for an opt-in feature most sessions won't enable.
`HybridLogicalClock::tick()` is a standard CAS-loop bump: advance the
physical component and reset logical to 0 if physical time moved forward;
otherwise bump logical. A default-constructed `HlcTimestamp{}` means "not
computed," the same sentinel convention `call_site`'s `is_known()` already
established.

One `HybridLogicalClock` lives on `Session` (not global) — unrelated
Sessions never contend on the same atomic. `Session::Config::causal_clock`
(default `false`) gates it: `Stream<T>::record()` checks this one boolean
before doing anything else, calling `session_.next_hlc_tick()` only when
true — the same "one branch when off" cost model already established and
measured negligible for `Stream<T>::RecordHook`
([ADR 0013](0013-tracy-bridge.md)).

`Stream<T>::snapshot_at_hlc(HlcTimestamp)` and the `tracked<T>` free-
function wrapper `chronicle::snapshot_at_hlc()` are the actual cross-stream
capability: `target` can come from a **different** tracked field's
`last_writer()->hlc`, unlike `snapshot_at_version()` which only ever makes
sense within the same stream. Implemented as a full linear scan, not an
early-break loop like the other `snapshot_at*()` methods — a real
correctness subtlety found while writing this, not by inspection alone:
computing an event's `hlc` (a CAS on the session's shared clock) and its
`version` (a `fetch_add` on the stream's own counter) are two separate,
non-atomic steps. A thread can be preempted between them, so under
concurrent producers a later-versioned event can carry an *earlier* `hlc`
than one that got its version first — `log_` staying version-sorted
([ADR 0009](0009-lock-free-ring-buffer.md)) does not guarantee it stays
`hlc`-sorted too. An early-break loop would silently return a wrong answer
in that window; a full scan tracking the best-qualifying event is correct
regardless of ordering. This is a cold query path, not `record()`'s hot
one, so the `O(n)` trade is the right one.

Wire format bumps v3 → v4 (`kFormatVersion`): every event header gains
`write_hlc()`/`read_hlc()` right after `call_site`, following the exact
same "zero means unknown, faithfully serialized, not a fallback" pattern
`call_site` already uses. A breaking, non-backward-compatible bump, same as
every prior one (v1→v2, v2→v3) — no compatibility commitment exists yet for
this format, and [ADR 0018](0018-v1-api-stability-commitment.md) explicitly
scoped the v1.0 API stability commitment to exclude it for exactly this
reason.

### Verification performed
`tests/unit/hlc_test.cpp` (9 tests): the primitive's tick-within-a-tick and
advance-on-new-tick behavior, defensive handling of an out-of-order
physical reading, the default/unknown sentinel, `Stream<T>::record()`
producing unknown `hlc` when disabled and monotonically increasing known
`hlc` when enabled, and — the actual point of this feature — a genuine
cross-stream query test: two different `tracked<T>` fields
(`player.health`, `player.zone`) mutated interleaved in one session,
capturing `zone`'s `last_writer()->hlc` and using it to correctly recover
`health`'s value "as of that same moment" via `snapshot_at_hlc()`, which
`snapshot_at_version()` has no way to express at all. A genuine concurrent
stress test (this project's established standard for new shared atomic
state, per ADR 0009's own precedent): 8 threads × 5,000 `tick()` calls each
on one shared clock, collecting all 40,000 results and confirming every one
is globally distinct and strictly ordered — stable across 5 consecutive
runs. `tests/unit/io_test.cpp` gained two round-trip tests confirming `hlc`
survives the on-disk format correctly both disabled (unknown) and enabled
(known, ordered). Full suite: **243/243 checks across 49 tests**.

Cost measured, not assumed: `bench/main.cpp` gained
`tracked_assignment_causal_clock`, measuring **86-104 ns/op** against
**63-67 ns/op** for the same benchmark with `causal_clock` disabled — a
real, consistent ~30-50% overhead from the CAS loop, unlike
`RecordHook`'s noise-floor-indistinguishable cost. See `bench/RESULTS.md`'s
new "Hybrid logical clock cost" section — this is exactly why the feature
defaults off.

## Consequences
- Positive: a real cross-stream ordinal query now exists
  (`snapshot_at_hlc()`) that per-stream version counters structurally
  cannot provide — the concrete capability this ADR set out to add.
- Positive: the timestamp-tie problem ADR 0007 found is structurally
  solved for any session that opts in, not just worked around per-query.
- Positive: zero cost for the (likely common) case of a session that
  never enables `causal_clock` — one branch, measured negligible.
- Negative: a real, non-trivial (~30-50%) per-event cost for sessions that
  do enable it — an honest trade-off, not hidden behind an averaged or
  best-case number.
- Negative: still does not, and cannot, solve genuine cross-thread causal
  ordering under data races — restated explicitly so this feature is never
  mistaken for more than it is, consistent with
  [04-technical-limitations.md](../04-technical-limitations.md)'s standing
  policy against overclaiming precision the language doesn't give us.
- Negative: format v4 is a breaking, non-backward-compatible bump from v3
  — consistent with every prior bump, and explicitly outside
  [ADR 0018](0018-v1-api-stability-commitment.md)'s API stability
  commitment, which scoped the wire format out for exactly this reason.
