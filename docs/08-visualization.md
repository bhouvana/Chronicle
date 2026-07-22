# Visualization & Developer Experience

## Process-separation decision (inherits from Phase 5)

No GUI, browser runtime, or windowing toolkit is ever linked into the instrumented
binary. Visualization tools are separate processes consuming the Query API / session
files. This is the same architecture Tracy, Perfetto, and WinDbg TTD all converged
on independently — it's not a novel choice, it's the only one that survives contact
with "can I ship this in a release game build."

## Tiered experience, matching audience maturity

1. **Terminal / CLI (`chronicle-cli`)** — always available, zero extra dependencies,
   works over SSH on a build farm or a headless CI job. `chronicle-cli history
   player_1.health`, `chronicle-cli diff session.chronicle t0 t1`. This is the tier
   that must exist first (v0.1, see Phase 10) because it's the cheapest to build and
   is a prerequisite dependency-free building block the other tiers call into.
2. **Static HTML export (`chronicle-cli export --html`)** — a single self-contained
   HTML file (inline JS/CSS, no external fetch) rendering a timeline/scrubber for a
   session, shareable as a build artifact or CI attachment the same way a Perfetto
   trace link is shared today. No server, no install — this is the highest
   leverage-to-effort visualization tier and should ship in v0.2 (Phase 10).
3. **Interactive browser viewer** — a longer-lived local web app (served by
   `chronicle-cli serve`) for live sessions: timeline scrubbing, object graph view,
   field-level diff highlighting, causal-chain graph ("what wrote this"). This is
   the tier that most directly answers Phase 8's brief ("should history become a
   graph? timeline? animation?") — answer: **timeline is primary** (matches the
   Recording Model's version-ordered nature directly), **object/ownership graph is
   secondary** (derived view, not the default), **full 3D/animation replay is a
   domain-specific adapter concern** (e.g. a game might render its `position`
   streams back into its own engine — Chronicle provides the data, not a renderer).
4. **VS Code extension** — inline gutter annotations ("this field changed 4 times in
   this session, click to see history"), reusing the browser viewer's webview. Later
   milestone (v1.0+, Phase 10) once the browser viewer is stable, since it's a thin
   shell over the same rendering code, not a separate viewer implementation.

## Interop over reinvention

- **Tracy bridge**: emit Chronicle mutation events as Tracy "plot" data points /
  zone messages when a Tracy connection is active, so users already running Tracy
  see history annotations inline in a tool they already trust, instead of Chronicle
  building its own competing flame-graph/profiler UI (explicit non-goal, Phase 3).
- **Perfetto/Chrome-trace export**: `chronicle-cli export --perfetto` emits
  Chronicle events as a Perfetto trace track, reusing Perfetto's mature timeline UI
  for users who prefer it over the native HTML viewer.
- Chronicle's own native viewer exists because Tracy/Perfetto have no concept of
  *object identity* or *field-level diff/replay* (Phase 2's gap) — the bridges are
  for teams who want Chronicle's data inside a tool they already standardized on,
  not a replacement for the native viewer's unique capabilities.

## Core interaction model: the scrubber

The primary UI metaphor is a **timeline scrubber directly over the version axis of a
`StateStream`** (or a merged view across several, aligned by best-effort timestamp
per Phase 6's causal-ordering note) — drag to any point, see the reconstructed
`Snapshot`, step event-by-event, and see the `Diff` highlighted between consecutive
steps. This is a deliberate, direct visualization of the Replay Engine's own
primitive (Phase 5) — the UI should never need to invent a view the underlying data
model doesn't already support natively; if a proposed visualization feature requires
new query capability, that capability belongs in the Query API first, the
visualization layer second.

## Accessibility & non-negotiables for the browser/VS Code tiers

Keyboard-navigable scrubber, colorblind-safe diff highlighting (never rely on
red/green alone — pair with +/- glyphs), and a text-equivalent (the CLI/JSON export)
for every visual affordance, so the terminal tier is never a second-class citizen —
directly serving CI/headless and screen-reader use cases from the same underlying
data rather than bolting on accessibility after the fact.
