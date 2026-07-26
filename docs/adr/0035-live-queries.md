# ADR 0035: Three Concrete Live Queries, Not a Query Language

## Status
Accepted

## Context
[docs/13-vision.md](../13-vision.md)'s Layer 7 names example questions:
"which variable changed most," "show all writes from thread 6." A general
query language over session data is real, open-ended scope (parsing,
a grammar, an execution engine) — this ships the concrete, bounded
version instead: specific answers this project can actually justify from
data it already has, following the same "ship what's real, not what's
speculative" discipline as every other feature this cycle.

## Decision
`tools/cli/query.hpp/.cpp`, three subcommands:
- `chronicle-cli query most-changed <file>` — every stream ranked by
  event count, descending.
- `chronicle-cli query threads <file>` — every distinct `thread_hash`
  seen in the file, given a friendly `0, 1, 2, ...` index in first-seen
  order (the raw hash is otherwise opaque and unusable as a human-facing
  identifier) plus its event count.
- `chronicle-cli query thread <file> <index>` — every event from that
  thread across every stream, chronologically merged.

`events_from_thread()` reuses `merge_object_history()`
([ADR 0031](0031-object-graph.md)/[0034](0034-object-snapshot.md)) by
constructing a synthetic `ObjectGroup` containing every stream in the
session, rather than writing a second merge/ordering implementation —
`merge_object_history()` only needs a name and a list of streams, not that
they share a naming prefix.

### Verification performed
Against a real session file: `most-changed` correctly ranked
`player.health` (3 events) above the three 2-event streams;
`threads` correctly reported exactly 1 distinct thread with 9 total
events (matching 3+2+2+2 across all four streams, a single-threaded
scratch program); `thread 0` correctly returned the merged, HLC-ordered
log across all streams for that thread; an out-of-range thread index
correctly errored with a clear message and exit code 1 rather than
printing nothing.

## Consequences
- Positive: real, bounded answers to two of Layer 7's named examples,
  with no new merge/ordering logic — a third reuse of
  `merge_object_history()`'s pattern this cycle.
- Positive: thread indices are stable and human-usable where the
  underlying `thread_hash` isn't.
- Negative: not a general query language — "which object allocates the
  most memory," "when did this invariant first fail" (other Layer 7
  examples) aren't answered by these three subcommands and would need
  their own scoped features, not a generic query engine, if pursued.
