# ADR 0003: Per-Stream Causal Ordering Instead of a Global Synchronized Clock

## Status
Accepted

## Context
Full deterministic global ordering across all threads/streams (as rr/UndoDB/WinDbg
TTD provide, see [../01-research-landscape.md](../01-research-landscape.md)) is one
of the largest unsolved problems in this space and was flagged as the top technical
risk in [../03-core-idea-and-feasibility.md](../03-core-idea-and-feasibility.md).
Committing the core recording format to a strict global total order would require
solving it before v0.1 could ship.

## Decision
Each event carries a causal predecessor within its own stream and a per-thread
monotonic sequence number. Cross-stream ordering uses wall-clock timestamps as a
best-effort approximation only, explicitly documented as such. A reserved metadata
field allows a future upgrade to a hybrid logical clock (see
[../12-future-research-topics.md](../12-future-research-topics.md) item 1) without
an on-disk format break.

## Consequences
- Positive: unblocks shipping v0.1 without solving distributed/multithreaded
  determinism — a stream's own history is always correctly ordered because it's
  serialized by whatever synchronization made the mutation safe in the first place.
- Positive: keeps the door open to a stronger guarantee later without a breaking
  format change, because the field is reserved now.
- Negative: cross-stream causal queries ("what was Y when X changed") are
  best-effort, not provably correct under races — must be marketed honestly
  (see [../04-technical-limitations.md](../04-technical-limitations.md)'s stance on
  never overclaiming precision the language doesn't actually give us for data races).
- Risk owner: this ADR is the canonical answer to "why doesn't Chronicle guarantee
  global ordering" for any future contributor or user who asks.
