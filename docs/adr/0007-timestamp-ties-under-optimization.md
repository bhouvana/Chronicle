# ADR 0007: Version-Based Snapshot Queries Are the Reliable Mechanism; Timestamp-Based Ones Are Confirmed Best-Effort

## Status
Accepted

## Context
[ADR 0003](0003-causal-not-global-ordering.md) already predicted, on paper,
that cross-event wall-clock ordering is best-effort and that per-stream
version order is the one ordering the system actually guarantees. This
stopped being a theoretical caveat during v0.2 development: building
`tracked_vector<T>`'s tests and running the full suite under a Release
(`-O2`) build — not just the Debug build used earlier — produced real,
reproducible test failures.

Two tests captured a `std::chrono::steady_clock::time_point` between fast,
back-to-back mutations (e.g. `push_back`; `update`; capture `t1`) and then
asserted an exact reconstruction as of that time_point. Under `-O2`, the
operations between two `now()` calls executed fast enough that consecutive
events could carry the *same* clock reading. `Stream<T>::snapshot_at()`'s
tie-breaking rule (`event.timestamp > at` to exclude, so ties are included)
then pulled in one extra event, producing an off-by-one-event snapshot and
failing size/value assertions
(`tests/unit/tracked_vector_test.cpp`: `snapshot_reconstructs_container_
contents_via_replay`, `container_diff_reports_updates_inserts_and_erases`).
The equivalent scalar test in `tracked_test.cpp` had the identical latent
bug — it happened not to tie in the runs observed, which is exactly the
kind of "passes by luck" result this ADR exists to stop relying on.

## Decision
Added an exact, tie-free query path alongside the existing timestamp-based
one, on both `Stream<T>` and the `tracked<T>`/`tracked_vector<T>` free
functions:
- `Stream<T>::current_version() -> std::uint64_t` — the version of the most
  recently recorded event.
- `Stream<T>::snapshot_at_version(std::uint64_t) -> optional<Snapshot<T>>` —
  reconstructs state as of an exact version, no clock involved.
- `chronicle::current_version(field)` / `chronicle::snapshot_at_version(field, v)`
  for both `tracked<T>` and `tracked_vector<T>`.

`snapshot_at(time_point)` is kept — wall-clock queries are still the right
tool when the caller genuinely wants "as of this moment in real time" (e.g.
correlating with an external log) — but is now documented on both the
`Stream<T>` method and this ADR as best-effort with a specific, confirmed
failure mode (ties under optimization), not a theoretical one. All tests
that need an exact reconstruction now use the version-based path; one test
(`timestamp_based_snapshot_still_works_as_a_best_effort_smoke_test`) keeps
`snapshot_at(time_point)` under test with only tie-tolerant assertions.

## Consequences
- Positive: the project's own documented ordering guarantee (ADR 0003) now
  has a concrete API surface, not just a caveat in prose — and it was added
  because a real test failure demanded it, not speculatively.
- Positive: `tests/unit/` no longer has a class of test that can fail
  depending on optimization level, compiler, or hardware clock resolution —
  confirmed via 20 consecutive Release-mode runs after the fix (0 failures).
- Negative: two APIs now exist for "get a snapshot" (time-based,
  version-based) with a caveat callers must understand to choose correctly.
  Judged acceptable — the alternative (only exact version-based queries) would
  remove a legitimate use case (correlating with real time), and the
  docstrings on both point at each other and at this ADR.
- Follow-on: `history()`'s own `HistoryRecord<T>` already exposes `version`
  alongside `timestamp`, so no existing API needed to change to make this
  possible — the fix was additive, not a breaking change to v0.1's surface.
