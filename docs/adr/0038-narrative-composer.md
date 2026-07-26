# ADR 0038: Narrative Composer — Emergent, Built From Persisted Data Only

## Status
Accepted

## Context
[docs/13-vision.md](../13-vision.md)'s Layer 10 describes an emergent
narrative capability ("Renderer stalled because Physics waited... Thread 3
blocked...") that "falls out of Layers 2 + 3 + 9 composed together... not
a mechanism of its own." A real constraint shapes what's actually
achievable here: Layers 3 ([ADR 0032](0032-provenance-stacktrace.md)) and
4 ([ADR 0033](0033-derived-state.md))'s registries are explicitly
**in-process only** — a full call-chain trace or a derivation's
explanation genuinely does not exist once the producing process exits, so
a CLI tool operating on a `.chronicle` file cannot honestly incorporate
them, no matter how the composition is written.

## Decision
`chronicle-cli narrate <file> <object> <position>` composes only what's
actually **persisted to disk**:
- Per-field reconstructed values at `position`, via `snapshot_fields()`
  (`tools/cli/object_graph.cpp`, extracted from `main.cpp`'s
  `print_fields_snapshot` in this same change so
  `object-snapshot`/`program-snapshot`/`narrate` share one fold
  implementation instead of three).
- Each field's real, persisted call site (`CallSiteInfo`, format v2
  onward, [ADR 0010](0010-call-site-capture.md)) — the lighter, always-on
  provenance every event already carries, not the heavier in-process-only
  full stack trace from ADR 0032.
- A cross-thread proximity pass that **mirrors `chronicle::possible_race()`'s
  exact logic** ([ADR 0023](0023-possible-race-query.md)) — same two
  conditions (both HLCs known, differing `thread_hash`), same default
  window — applied to `LoadedEvent` fields directly instead of live
  `HistoryRecord<T>`, since this runs at the type-erasure boundary
  [ADR 0008](0008-cli-avoids-streambase-virtual-dispatch.md) already
  established chronicle-cli operates behind. Not a new race-detection
  mechanism — the same one, restated for file data.
- A structural-anomaly pass mirroring `container_growth_report()`/
  `is_likely_leak()` ([ADR 0026](0026-anomaly-detection.md)) — replayed
  over `LoadedStream`'s `ContainerOpKind` ops directly (the same
  Insert/Erase/Clear fold, generically over `WireValue` instead of a
  typed `ContainerOp<T>`, the same kind of type-erasure-boundary
  reimplementation `replay_indexed`/`replay_keyed` already established as
  accepted practice in this codebase).

### Verification performed
Two real session files. The existing 4-field, 2-object demo: `narrate`
correctly showed each field's value with its real call site, including
`"unknown call site"` for fields written via plain `=` assignment (which
structurally cannot capture one, per ADR 0010) versus a real
`file:line` for a field written via `push_back()`. A new, deliberately
provocative dataset (an 8-item vector that's never erased from, past the
leak threshold, plus two real threads racing on `causal_clock`-enabled
scalar fields): the structural-anomaly note correctly fired
("grew to 8 item(s), never shrunk"), and the cross-thread race note
correctly fired at the position where the two threads' real HLCs actually
landed within the window — both passes verified to actually trigger on
real data, not just compile without ever firing. Full suite unaffected:
**405/405 checks across 102 tests** (this feature touches only
`tools/cli`).

## Consequences
- Positive: Layer 10 is real and working, built entirely from data this
  project can honestly claim to have — no fabricated access to
  in-process-only registries.
- Positive: three existing algorithms (snapshot folding, race detection,
  growth detection) got their type-erased-boundary counterparts reused or
  written once, not scattered across ad hoc one-off logic.
- Negative: `narrate`'s race/growth passes are **restatements** of
  `possible_race()`/`container_growth_report()`'s logic, not calls to the
  same function — the type-erasure boundary means this project now has
  two implementations of each algorithm (typed, for live `Session`s; and
  `WireValue`-based, for loaded files) that could in principle drift
  apart, the same accepted tradeoff `replay_indexed`/`replay_keyed`
  already represents for container replay.
- Negative: no full call-chain provenance (ADR 0032) or derivation
  explanations (ADR 0033) in the narrative — genuinely unavailable until
  persistence for those registries is built (docs/13-vision.md's own
  top-priority remaining item).
