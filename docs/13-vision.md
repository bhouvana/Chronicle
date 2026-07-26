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

**Status as of this cycle**: all 10 layers now have a real disposition
and, where built, an ADR. Layers 1-5, 6 (scoped), 7 (partial), 9 (one
bridge), and 10 are done. Layer 8 is extended. Nothing here reopens or
reverses a decision this project's ADRs already made — Layer 6 was
completed in its explicitly-scoped form ("rewind everything Chronicle
instruments," not literal memory-level determinism), Layer 9 shipped
exactly one bridge that could be verified for real in this environment
rather than fabricating the others, and Layer 10 composes only data
that's actually persisted to disk. Every new layer either extended an
existing extension point (`RecordHook`, the `(stream_id, version)`
registry pattern, `merge_object_history()`) or added a small, additive,
real capability with its own measured cost where one existed to measure.

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

## Layer 6 — Program Time Machine — **done, in scoped form** (this cycle) — [ADR 0036](adr/0036-whole-program-rewind.md)
Read literally ("rewind the entire application — memory, objects,
containers, everything"), this is full deterministic whole-program
replay — and that literal version remains **explicitly capped, not
attempted, do not reopen without new evidence**. This project already
evaluated it (docs/12 topic 1's v2.0 research spike) and found real,
measured reasons not to: even a *partial*, pairwise-only HLC slice cost
~30-50% per-event overhead ([ADR 0019](adr/0019-hybrid-logical-clock.md));
a full vector-clock/happens-before graph would cost substantially more for
a class of value (`possible_race()`, ADR 0023) this project already
provides more cheaply; `rr`-style deterministic scheduling underneath
Chronicle conflicts with the foundational source/API-level-only
instrumentation choice made before v0.1
([03](03-core-idea-and-feasibility.md)/[04](04-technical-limitations.md)).
None of that evidence changed this cycle.

What *did* ship is exactly the scoped version this entry always
prescribed as the honest path: `chronicle-cli program-history`/
`program-snapshot` — "rewind everything Chronicle actually instruments,
best-effort across threads," mechanically the same
`object-history`/`object-snapshot` merge (ADR 0031/0034) applied to every
stream in the session rather than one object's fields. No new mechanism,
no reopened architectural question.

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

## Layer 9 — Runtime Knowledge Graph — **one real bridge done** (this cycle) — [ADR 0037](adr/0037-filesystem-bridge.md)
Network, GPU, filesystem, coroutines as streams, achieved by *bridging* to
existing systems the way the Tracy and Perfetto bridges already do, not by
Chronicle reimplementing an APM platform. This cycle shipped exactly one:
`chronicle::bridges::TrackedFile`, real file I/O (open/read/write/close)
as `tracked<FileOp>` events, verified against a real temporary file (a
real write-then-read round trip, not a stringstream stand-in) — chosen
specifically because it needed no external dependency and no hardware/OS
feature this environment couldn't also verify against for real. Network
and GPU bridges remain real, unattempted future work: each would need an
actual network stack or GPU context to verify honestly, which fabricating
without one would violate this project's own "verify for real" standard.
Each new domain stays its own scoped bridge decision, not a single
"add everything" effort — this entry is proof of that discipline, not a
reason to relax it for the remaining three.

## Layer 10 — Engineering Memory — **done, from persisted data only** (this cycle) — [ADR 0038](adr/0038-narrative-composer.md)
The "Renderer stalled because Physics waited..." narrative is what Layers
2 + 3 + 9 composed together would ideally produce — but Layers 3/4's
registries are in-process only (ADR 0032/0033), so a `.chronicle`-file-based
narrator genuinely cannot include full call chains or derivation
explanations, no matter how it's composed. `chronicle-cli narrate`
composes exactly what *is* persisted: object-snapshot values, real
per-event call sites, a `possible_race()`-mirroring cross-thread pass, and
a container-growth pass — verified to actually fire on real, deliberately
provocative data (a real leak-shaped vector, two real racing threads), not
just to compile without ever triggering. The fuller version — including
full call chains — falls out for free once Layer 3/4 persistence (this
document's own top remaining-work item) exists; not attempted
speculatively ahead of that.

## How to use this document
All 10 layers now have a real, honest disposition and, where something was
actually built, an ADR to show for it. Picking this up again means
extending what's here, not re-deriving a starting point — hold every
extension to this project's real, measure-don't-assume standard, and treat
the Layer 6 caution (the literal, unscoped version) as a permanent hard
gate, not a suggestion that expires.

The real, concretely-scoped remaining work, in rough priority order:
1. **Persistence for Layers 3/4's in-process-only registries** — one
   shared wire-format decision (format bump + a real storage-cost call for
   variable-length per-event data), not two separate ones. This is the
   single highest-leverage remaining item: it directly unblocks a fuller
   Layer 10 narrative (real call chains, not just call sites) and gives
   Layer 6's `program-snapshot` access to provenance it doesn't have today.
2. **Composable multi-hook dispatch on `RecordHook`** — unblocks Layer 4's
   single-hook limit and lets provenance/derivation/Tracy coexist on one
   field instead of competing for the one slot.
3. **Layer 7's remaining named queries** ("which object allocates the
   most," "when did this invariant first fail") — each its own small,
   scoped addition, not a general query engine.
4. **A second Layer 9 bridge** (network or GPU) — only once there's a real
   system available to verify it against honestly, the same bar the
   filesystem bridge was held to.
