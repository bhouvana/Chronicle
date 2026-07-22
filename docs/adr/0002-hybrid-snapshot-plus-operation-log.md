# ADR 0002: Recording Model is Periodic Snapshot + Operation Log per Stream

## Status
Accepted

## Context
See [../06-recording-model.md](../06-recording-model.md) for the full strategy
survey. The candidates were: full snapshot per mutation (unbounded growth),
operation-log-only (unbounded replay cost from session start), memory-page
snapshotting (wrong granularity, OS-specific), and persistent/immutable trees
(right for container-shaped data, not scalar fields generally).

## Decision
Each `StateStream` records small operation-log events between periodically inserted
full snapshots, with the interval configurable per stream (event count, byte
threshold, or explicit checkpoint). Reconstructing any version requires walking at
most one snapshot interval's worth of events.

## Consequences
- Positive: bounds replay cost without paying full-snapshot memory per mutation —
  directly resolves the "unbounded storage growth" risk identified in
  [../03-core-idea-and-feasibility.md](../03-core-idea-and-feasibility.md).
- Positive: works uniformly for both scalar fields (event payload = new value) and
  containers (event payload = structural delta), needing only a different payload
  encoding, not a different model.
- Negative: reconstructing an arbitrary version is O(snapshot interval), not O(1) —
  judged acceptable since Replay/Query are cold-path by design (Phase 5) and the
  interval is tunable per stream to trade memory against replay latency.
- Superseded-by: none yet. A future ADR would be required to change this model,
  e.g. if the persistent-tree approach proves superior for a specific adapter class
  (see [../12-future-research-topics.md](../12-future-research-topics.md) for open
  questions that could motivate this).
