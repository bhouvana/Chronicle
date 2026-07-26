# ADR 0023: `possible_race()` Query on Top of the HLC

## Status
Accepted

## Context
[docs/12-future-research-topics.md](../12-future-research-topics.md) topic 1's
v2.0 research spike findings named a concrete, cheap follow-up that the spike
itself didn't build (out of scope for an evaluation): a query like
`chronicle::possible_race(event_a, event_b)`, returning true when two events'
HLCs ([ADR 0019](0019-hybrid-logical-clock.md)) are close together and their
threads differ — the shape of "apparent race" [ADR 0003](0003-causal-not-global-ordering.md)
and [04-technical-limitations.md](../04-technical-limitations.md) already
call for surfacing as flagged uncertainty, never silently resolved.

## Decision
`include/chronicle/race.hpp`: `chronicle::possible_race(HistoryRecord<T> const&, HistoryRecord<U> const&, std::uint64_t window_us = 0)`.
Templated on two possibly-different `T`/`U` since the whole point (per ADR
0019) is comparing across different `tracked<T>`/`tracked<U>` fields. Returns
`false` outright when either side's `hlc` is unknown (no fabricated answer
when `causal_clock` was off) or when both events share a `thread_id` (ADR
0003's per-thread program order already resolves that case, regardless of
timing). Otherwise compares `physical_us` distance against `window_us`;
default `0` matches only events sharing the exact same physical tick, the
narrowest, least-false-positive-prone definition. No new state, no new wire
format field — reuses `HlcTimestamp` comparison `snapshot_at_hlc()` already
does.

Deliberately scoped as "may have raced," not "did race": this project's
source-level instrumentation has no synchronization information to answer
the stronger question (same honesty bar as every other blind-spot statement
in 04-technical-limitations.md).

### Verification performed
`tests/unit/race_test.cpp` (5 tests): disabled-clock returns false; same-
thread events return false regardless of closeness; two real threads (not
simulated timestamps) racing to record return true under a wide window and
confirm `thread_id` genuinely differs; two real threads separated by a 50ms
sleep return false under a 1ms window; the zero-default window returns false
for two events 5ms apart. Full suite: **264/264 checks across 58 tests**.

## Consequences
- Positive: closes the one concrete gap the v2.0 spike explicitly left open,
  using only machinery that already existed (HLC) — no new cost, no new
  format version.
- Positive: additive to the ADR 0018-covered public surface (a new free
  function under the umbrella header), not a signature change to anything
  existing.
- Negative: inherits every limitation `snapshot_at_hlc()` already has —
  meaningless across different `Session`s (topics 5/6), and, like ADR 0019
  itself, cannot detect a race that never touched a `causal_clock`-enabled
  `tracked<T>` field.
