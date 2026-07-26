# ADR 0034: Object Snapshot — "One Slider, Entire Object"

## Status
Accepted

## Context
[docs/13-vision.md](../13-vision.md)'s Layer 5 asks for `history(player)`
as "one slider, entire object" — scrubbing a whole object's state at once,
not one field at a time. [ADR 0031](0031-object-graph.md)'s
`object-history` already merges every field under an object into one
chronologically-ordered log; this is the missing half: reconstructing the
*full state* of every field as of one point in that log, not just listing
events.

## Decision
`chronicle-cli object-snapshot <file> <object> <position>`. `position` is
an **index into the object's own merged event log**
(`chronicle_cli::merge_object_history()`, extracted from
`cmd_object_history` into `tools/cli/object_graph.cpp` in this same change
so `object-history` and `object-snapshot` share one merge implementation
instead of two that could silently drift apart) — deliberately not a raw
version number or timestamp, since per-stream versions have no shared
meaning across different fields ([ADR 0003](0003-causal-not-global-ordering.md))
and a fabricated absolute clock position would overclaim precision this
project doesn't have. "The slider" is honestly the one merge this project
can actually justify, not a fictional independent clock.

For each field under the object: scalar fields take the last value at or
before `position`; `IndexedOp`/`KeyedOp` fields replay
(`replay_indexed`/`replay_keyed`, already shared with `cmd_diff` via
`tools/cli/replay.hpp`) up to the highest version that field reached at or
before `position`. A field with no event yet at that position reports
"(not yet recorded)" rather than a fabricated default value.

### Verification performed
Against a real generated multi-field session file: position 0 correctly
shows only the first-ever recorded field with everything else
"(not yet recorded)"; a mid-range position correctly shows a partial mix
of recorded/not-yet-recorded fields with correct values; a
past-the-end position correctly clamps to the last real position and
shows the full final state (matching the known final values exactly,
including the vector field's full `["sword", "shield"]` contents).
`object-history` re-verified unchanged after the shared-merge refactor.

## Consequences
- Positive: real "one slider, entire object" scrubbing, in text form,
  reusing existing replay primitives rather than a third implementation.
- Positive: `object-history`/`object-snapshot` can no longer drift apart
  on ordering — one merge function, two consumers.
- Negative: "position" is a CLI-only concept with no meaning outside this
  merged view — it's not a version, timestamp, or anything a caller could
  reconstruct without re-running the same merge. Acceptable for a
  debugging tool, not something to expose as a stable identifier elsewhere.
