# ADR 0036: Whole-Program Rewind, Scoped Honestly

## Status
Accepted

## Context
[docs/13-vision.md](../13-vision.md)'s Layer 6, read literally ("rewind the
entire application — memory, objects, containers, everything"), is full
deterministic whole-program replay. This project already evaluated exactly
that (docs/12 topic 1's v2.0 research spike) and found real, measured
reasons not to: even a partial, pairwise-only HLC slice costs ~30-50%
per-event overhead ([ADR 0019](0019-hybrid-logical-clock.md)); a full
vector-clock/happens-before graph would cost substantially more for value
`possible_race()` ([ADR 0023](0023-possible-race-query.md)) already
provides more cheaply; and `rr`-style deterministic scheduling underneath
Chronicle conflicts with the foundational source/API-level-only
instrumentation choice made before v0.1
([03](03-core-idea-and-feasibility.md)/[04](04-technical-limitations.md)).
None of that evidence has changed. This ADR does not reopen it.

What the vision doc's own text already prescribed as the honest path,
should this layer be pursued at all: **"rewind everything Chronicle
actually instruments, honestly labeled as best-effort across threads,"
not "everything, memory included."** This ADR builds exactly that, nothing
more.

## Decision
`chronicle-cli program-history <file>` and
`chronicle-cli program-snapshot <file> <position>` — mechanically
identical to `object-history`/`object-snapshot`
([ADR 0031](0031-object-graph.md)/[0034](0034-object-snapshot.md)), scoped
to *every* stream in the session instead of one object's fields.
`merge_entire_session()` (`tools/cli/object_graph.cpp`) builds a synthetic
`ObjectGroup` containing every stream and calls the existing
`merge_object_history()` — no second merge/ordering algorithm. This also
replaced `query.cpp`'s own inline duplicate of the same "treat every
stream as one group" construction (ADR 0035's `events_from_thread()`),
removing a small piece of drift risk that already existed between the two.

This inherits every caveat `object-history`/`object-snapshot` already
state, now at full-program scope: best-effort HLC-or-elapsed_ns ordering
(never a stronger claim than ADR 0003/0019), no real synchronization
information beyond what `causal_clock` provides, and — because this is
explicitly the *scoped* interpretation — no claim about raw memory,
un-instrumented state, or true deterministic replay at all. A caller
reading "program-snapshot" should understand "every field Chronicle was
told to track, reconstructed at a position in a best-effort merge," not
"the process's actual memory image."

### Verification performed
Against a real 4-stream, 2-object session file: `program-snapshot` at
position 0 correctly shows only the first-ever-recorded field with every
other field "(not yet recorded)"; at the clamped-to-end position, all 4
fields across both objects show their correct final values (matching
known data exactly, including a vector field's full contents);
`program-history` correctly reports all 9 events across all 4 streams in
HLC order. Full `chronicle-core-tests` suite unaffected (this touches only
`tools/cli`).

## Consequences
- Positive: a real, honestly-scoped answer to Layer 6 that required zero
  new merge/ordering logic — a fourth reuse this cycle of
  `merge_object_history()`'s pattern (after `object-snapshot`,
  `diff-runs`-adjacent, and `query thread`).
- Positive: consolidating `query.cpp`'s inline duplicate into
  `merge_entire_session()` removed real (if small) drift risk between two
  independently-written "treat the whole session as one group"
  constructions.
- Negative: still not full deterministic replay, and cannot become it
  without revisiting ADR 0003/the deterministic-replay spike's findings
  with genuinely new evidence — this ADR is not that evidence, and does
  not claim to be.
