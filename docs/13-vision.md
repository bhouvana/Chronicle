# Vision: Chronicle as the Runtime Memory System for C++

Not a roadmap ([10-roadmap.md](10-roadmap.md) is that) and not committed
research scope ([12-future-research-topics.md](12-future-research-topics.md)
is that) — a long-horizon framing for where this project could go, recorded
so it isn't lost, and so a future session evaluates each layer against
this project's actual architectural constraints rather than starting from
enthusiasm alone. Every layer below is either **done** (with an ADR),
**open direction** (a real next candidate), or **explicitly cautioned**
(collides with a settled decision and needs new evidence, not just
ambition, to reopen).

The framing: today, every large C++ codebase independently rebuilds state,
ownership, lifetime, logging, serialization, undo, replay, snapshots,
audit logs, and state synchronization as separate, incompatible concepts.
Chronicle's bet is that a single recording substrate can underlie all of
them — not by building each one, but by making "what changed, when, why,
and as part of what" answerable once, well, and reused everywhere.

**Status as of this cycle**: Layers 1-5 and 7 are done or partially done,
each with a real, verified, honestly-scoped feature and an ADR. Layer 8 is
extended. Layer 6 is explicitly capped pending new evidence. Layers 9 and
10 remain deliberately unbuilt — 9 because it's real scope only through
bridging (each new bridge its own decision, not attempted speculatively
here), 10 because it's an emergent property of the others, not a
workstream. Nothing here reopens or reverses a decision this project's
ADRs already made; every new layer either extended an existing extension
point (`RecordHook`, the `(stream_id, version)` registry pattern) or added
a small, additive, real capability with its own measured cost where one
existed to measure.

## Layer 1 — State Recording — **done** (v0.1 onward)
`tracked<T>`/`tracked_vector<T>`/`tracked_map<K,V>`, `history()`/
`snapshot()`/`diff()`/`replay`. This is [10-roadmap.md](10-roadmap.md)'s
entire v0.1–v2.0 arc. Complete, but — on its own — "useful," not yet
"indispensable."

## Layer 2 — Relationships — **done** (this cycle) — [ADR 0031](adr/0031-object-graph.md)
Objects derived from the existing dot-separated naming convention
(`player.health`/`player.mana` → object `player`), queryable from both a
live `Session` and the CLI (`chronicle-cli objects`/`object-history`).
Deliberately does NOT include object lifetime or cross-object references
— see ADR 0031's explicit scope boundary. Those are the natural Layer 2
follow-ons, open direction, not committed:
- **Cross-object references** ("Weapon referenced by Player") needs an
  explicit relationship-registration API — a real, additive feature, not
  inferable from naming.
- **Object lifetime** ("created at v51, destroyed at v90") needs an
  explicit tombstone/lifecycle marker — Chronicle has no way to
  distinguish "retired" from "just idle" without one, and inferring it
  from silence would be a fabricated claim.

## Layer 3 — Provenance — **done** (this cycle) — [ADR 0032](adr/0032-provenance-stacktrace.md)
`chronicle::set_with_stacktrace()`/`provenance_of()`, built on C++23
`std::stacktrace`, verified on both compilers this project's CI matrix has
(MSVC 19.44, Clang 21.1.6). The real, measured cost turned out far larger
than the "needs measuring" note originally here anticipated — **116,787.63
ns/op**, ~1,300-1,850x a plain tracked write, three orders of magnitude
past even `causal_clock`'s cost. That evidence directly shaped the design:
a separate, differently-named, explicit per-call opt-in, never a
`Session::Config` flag, never on `record()`'s hot path. In-process only —
**not yet persisted to the `.chronicle` wire format**; that's the real,
concretely-scoped next follow-on for this layer (a new format bump, plus a
real decision about storing variable-length per-event trace data, and only
then a `chronicle-cli` surface for it).

## Layer 4 — Derived State — **done** (this cycle) — [ADR 0033](adr/0033-derived-state.md)
`gold = income - tax`, auto-explained. The fork this entry originally
named is resolved: **(a)**, the explicit reactive API — `chronicle::derive()`/
`explain()`, built entirely on the existing `Stream<T>::RecordHook`
extension point (the same one the Tracy bridge already uses), not (b)
static-analysis-based dependency inference, which would be a project on
the scale of `tools/codegen`'s Clang tool and was deferred as genuinely
different, larger scope. Real, stated limits: `RecordHook` is single-slot
per stream (can't compose with e.g. a Tracy hook on the same dependency),
no derived-of-derived (no dependency-graph ordering/cycle detection
attempted), in-process only. The natural follow-ons, open direction, not
committed: composable multi-hook dispatch (would unblock both limits at
once), and persistence — shared with Layer 3's provenance follow-on, since
both are in-process-only for the same underlying reason (no wire-format
support for arbitrary per-event side-channel data yet).

## Layer 5 — Object Time Machine — **done** (this cycle) — [ADR 0034](adr/0034-object-snapshot.md)
`history(player)` instead of one field at a time is `object-history`
(ADR 0031); "one slider, entire object" is now real too:
`chronicle-cli object-snapshot <file> <object> <position>` reconstructs
every field's full value at a position in the object's own merged
history log. `position` is deliberately an index into that merge, not a
fabricated absolute clock — per-stream versions have no shared meaning
(ADR 0003), so the slider is honestly "the one merge this project can
justify," not an independent timeline. Still text-mode, not a visual
scrubber — a real browser-UI version is Layer 2/5 + a UI, genuinely
separate (and smaller) future work if the interactive viewer
(ADR 0016) is ever extended to read `object-snapshot`'s same logic.

## Layer 6 — Program Time Machine — **explicitly cautioned, do not attempt without new evidence**
"Rewind the entire application — memory, objects, containers, everything"
is, read literally, full deterministic whole-program replay. This project
already evaluated exactly this (docs/12 topic 1's v2.0 research spike) and
found: (a) even a *partial*, pairwise-only HLC slice cost a real, measured
~30-50% per-event overhead ([ADR 0019](adr/0019-hybrid-logical-clock.md));
(b) a full vector-clock/happens-before graph would cost substantially
more, for a class of value (`possible_race()`, ADR 0023) this project can
already provide more cheaply; (c) `rr`-style deterministic scheduling
underneath Chronicle conflicts with the foundational source/API-level-only
instrumentation choice made before v0.1
([03-core-idea-and-feasibility.md](03-core-idea-and-feasibility.md),
[04-technical-limitations.md](04-technical-limitations.md)) — it would
mean absorbing or reimplementing a different project's worth of scope.
**If this layer is pursued, it must be scoped down to "rewind everything
Chronicle actually instruments, honestly labeled as best-effort across
threads," not "everything, memory included" — reopening the literal
version needs new evidence, not renewed ambition.**

## Layer 7 — Live Queries — **partially done** (this cycle) — [ADR 0035](adr/0035-live-queries.md)
"Which variable changed most," "show all writes from thread 6" are real
now: `chronicle-cli query most-changed`/`threads`/`thread <index>`.
Deliberately three concrete, bounded answers, not a general query
language — "which object allocates the most memory," "when did this
invariant first fail" (this layer's other named examples) remain open,
real, separately-scoped candidates if pursued, not solved by a generic
query engine.

## Layer 8 — Event Intelligence — **extended** (this cycle) — [ADR 0026](adr/0026-anomaly-detection.md)
`range_anomalies()` (numeric, causal z-score) plus, this cycle,
`container_growth_report()`/`is_likely_leak()` — the exact "size always
grows, never shrinks... possible leak" structural/shape example this
layer named, for `tracked_vector<T>`. docs/12 topic 8's own caution still
applies at any larger scope: real risk of becoming a distraction from the
core value proposition if pursued before the query/replay experience is
proven in the wild. Remaining open direction: rate-of-change detectors
(derivative, not just level); still explicitly not chasing "AI" framing
beyond what's actually statistical.

## Layer 9 — Runtime Knowledge Graph — open direction, via bridging not reimplementation
Network, GPU, filesystem, coroutines as streams. Achieved by *bridging* to
existing systems the way the Tracy and Perfetto bridges already do, not by
Chronicle reimplementing an APM platform. Each new domain (network,
GPU, ...) is its own scoped bridge, evaluated on its own merits — not a
single "add everything" effort.

## Layer 10 — Engineering Memory — emergent, not a separate workstream
The "Renderer stalled because Physics waited because Thread 3 blocked..."
narrative is what Layers 2 + 3 + 9 composed together produce, not a
mechanism of its own. Don't plan engineering time against "Layer 10"
directly — it falls out of the others being done well.

## How to use this document
When picking up this vision again: check which layer is genuinely next
given what exists (the same exercise that picked Layer 2 this cycle —
look for what's already seeded, per ADR 0016 → ADR 0031's pattern), hold
every new layer to this project's real, measure-don't-assume standard, and
treat the Layer 6 caution above as a hard gate, not a suggestion. The
real, concretely-scoped remaining work, in rough priority order: (1)
persistence for Layers 3/4's in-process-only registries (one shared
wire-format decision, not two), (2) composable multi-hook dispatch on
`RecordHook` (unblocks Layer 4's single-hook limit and lets provenance/
derivation/Tracy coexist on one field), (3) Layer 7's remaining named
queries, (4) a real, evaluated first bridge for Layer 9 if one is worth
building. None of these require reopening Layer 6.
