# ADR 0031: Object/Ownership Graph, Derived From the Naming Convention

## Status
Accepted

## Context
[docs/13-vision.md](../13-vision.md) records a 10-layer, 10-year vision for
Chronicle ("the runtime memory system for C++"). Layer 2 ("relationships")
asks for `Player`/`Inventory`/`Weapon`-style object structure, not just a
flat list of independently-tracked fields — "show everything connected to
Player," "what changed under this object?"

This isn't starting from zero: [ADR 0016](0016-interactive-browser-viewer.md)'s
`renderObjectGraph()` already groups streams by name prefix
(`"player.health"`/`"player.mana"` → object `player`) in the browser
viewer's JS, purely as a derived display grouping from `track()`'s own
naming convention — "not a separate ownership model Chronicle tracks
explicitly," in that ADR's own words. That grouping was invisible
everywhere else: no C++ API, no CLI command, no tests.

## Decision
Make "object" a first-class, queryable concept in two places, both
deriving it from the exact same rule ADR 0016 already established
(split on the last `.`; a name with no `.` is its own single-field
object) — explicitly **not** a new relationship model:

1. **Core library** (`include/chronicle/object_graph.hpp`):
   `object_name_of(std::string const&)` (the splitting rule, in one
   place), `object_names(Session const&)`, and
   `field_names_of(Session const&, std::string const&)`. Required one new
   `Session` accessor, `stream_names()`, exposing each owned stream's name
   (via the already-public `StreamBase::name()`) without exposing the
   streams themselves. Added to the umbrella header — small, additive,
   always available, same category as `possible_race()`.

2. **CLI** (`tools/cli/object_graph.hpp/.cpp`): `group_by_object()` for
   `LoadedSession`s, reusing `chronicle::object_name_of()` directly (a
   plain `std::string` function with no `Session` dependency) rather than
   re-deriving the splitting rule a third time. Two new subcommands:
   `chronicle-cli objects <file>` (lists each object, its fields, and
   event counts — a CLI-visible version of the browser's Objects panel)
   and `chronicle-cli object-history <file> <object>` (merges every field
   under that object into one chronologically-ordered log — a text-mode
   answer to the vision's Layer 5 "history(player), not history(player.health)").

**Ordering in `object-history`, stated precisely**: merging events from
independent streams has no free ordering — per-stream version counters
don't compare across streams at all ([ADR 0003](0003-causal-not-global-ordering.md)).
When *every* merged event has a known HLC ([ADR 0019](0019-hybrid-logical-clock.md)),
the merge sorts by HLC — the one real cross-stream-comparable ordinal this
project has. Otherwise it falls back to `elapsed_ns`, labeled
"best-effort" in the output itself, not silently presented as exact. This
is not a new causal claim; it reuses exactly what ADR 0003/0019 already
established.

**Explicit scope boundary — what this does NOT do**, so it doesn't get
mistaken for more later: no object *lifetime* ("created at v51, destroyed
at v90") — Chronicle has no mechanism to distinguish "this field was
intentionally retired" from "this field is just idle," and inferring
lifetime from silence would be a fabricated claim, exactly the kind
[04-technical-limitations.md](../04-technical-limitations.md) polices
against. No cross-object *references* ("Weapon referenced by Player") —
that's a genuinely different, explicit relationship-registration mechanism
this project doesn't have; a real, separate future increment
(docs/13-vision.md, Layer 2 follow-up), not attempted here.

### Verification performed
`tests/unit/object_graph_test.cpp` (5 tests): the splitting rule's edge
cases (no dot, multiple dots, trailing dot), grouping order (first-seen,
not sorted), correct field membership per object, empty result for an
object with no fields, and the no-dot single-field-object case.

CLI: generated a real `.chronicle` file (a standalone program, 3 fields
under `player` — 2 scalars, 1 vector — and 1 under `match`, with
`causal_clock` enabled). `chronicle-cli objects` correctly reported
`player (3 field(s), 7 event(s) total)` and `match (1 field(s), 2
event(s) total)` with correct per-field shapes/counts.
`chronicle-cli object-history player` correctly merged and HLC-ordered
all 7 events across the 3 fields (`player.health` v0-v2, `player.mana`
v0-v1, `player.inventory` v0-v1), each with correct values and call
sites; querying a nonexistent object correctly errored (exit code 1)
rather than printing nothing silently. Full `chronicle-core-tests` suite:
**352/352 checks across 89 tests**, no regressions.

## Consequences
- Positive: "object" is now a real, tested, queryable concept in both the
  live in-process API and the offline CLI, not just a browser-only display
  trick — Layer 2 of docs/13-vision.md is genuinely done, honestly scoped.
- Positive: `object-history` directly improves the value of features
  shipped earlier this cycle — e.g. `diff-runs`/`merge` become more useful
  once you can ask "what changed under this whole object," not just one
  field.
- Positive: zero duplicated splitting logic between the live-Session path
  and the CLI path (the CLI reuses `chronicle::object_name_of()`
  directly) — only the browser's JS remains an unavoidable third
  implementation, per ADR 0016's own note.
- Negative: grouping is single-level and purely syntactic — `a.b.c` groups
  under `a.b`, not `a`; a name change breaks its object membership with no
  migration story. A real hierarchical or explicitly-declared ownership
  model is future scope, not this increment.
- Negative: `object-history`'s best-effort fallback ordering
  (`elapsed_ns`) inherits every caveat wall-clock-based ordering already
  has in this project (ADR 0007's tie problem, ADR 0003's non-guarantee)
  — stated in the ADR and in the command's own output, not hidden.
