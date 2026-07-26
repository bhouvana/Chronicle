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

## Layer 4 — Derived State — open direction, needs a design decision first
`gold = income - tax`, auto-explained. This is a real fork, not a natural
extension of Layer 1: either (a) an explicit reactive/signals API the
caller opts a field into (buildable, but a second programming model
alongside the recording one), or (b) inferring dependencies from source
via static analysis (a much larger, genuinely research-shaped
undertaking, closer to what a compiler or datalog engine does). Don't
scope this until which one is meant is decided.

## Layer 5 — Object Time Machine — mostly done, compositionally
`history(player)` instead of one field at a time is exactly
`object-history` (ADR 0031). A visual "scrub through one object" is Layer
2 + the existing per-field replay + a UI, not a new mechanism.

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

## Layer 7 — Live Queries — open direction
"Which variable changed most today," "show all writes from thread 6" —
a query language over `LoadedSession`/live `Session` data. Buildable
incrementally on top of Layers 1-3; no new recording mechanism needed,
just a richer query surface than today's fixed CLI subcommands.

## Layer 8 — Event Intelligence — partially done — [ADR 0026](adr/0026-anomaly-detection.md)
`range_anomalies()` (this cycle) is exactly the "oscillating state,"
"always grows, never shrinks" pattern-detection this layer asks for,
generalized as a causal online z-score. docs/12 topic 8's own caution
still applies at any larger scope: real risk of becoming a distraction
from the core value proposition if pursued before the query/replay
experience is proven in the wild. Extend incrementally (rate-of-change
detectors, container-shape anomalies), don't chase "AI" framing beyond
what's actually statistical.

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
treat the Layer 6 caution above as a hard gate, not a suggestion.
