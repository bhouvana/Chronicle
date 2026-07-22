# ADR 0016: `chronicle-cli serve` Reuses the Static Export's Renderer; No Live Transport Yet

## Status
Accepted

## Context
[10-roadmap.md](../10-roadmap.md)'s v1.0 scope calls for an "interactive
browser viewer (`chronicle-cli serve`) with live scrubbing, object/ownership
graph view." [08-visualization.md](../08-visualization.md) describes this as
tier 3: "a longer-lived local web app... for live sessions: timeline
scrubbing, object graph view, field-level diff highlighting, causal-chain
graph."

Two things were checked before writing any code, not assumed:

- **No live transport exists.** "For live sessions" reads as "while a
  producer process is still recording," but nothing in this codebase
  streams data out of a running process — docs/06's "stream live to an
  external viewer process over a local socket" is unimplemented, the exact
  gap [ADR 0014](0014-storage-engine-compression.md) already found and
  deferred LZ4 live-stream compression on. Building a true live-updating
  server now would mean designing a whole socket protocol as a rushed
  afterthought inside what's supposed to be a visualization milestone — out
  of scope here, deferred alongside LZ4 for the same reason.
- **The static export (v0.2, `chronicle-cli export --html`) already
  implements timeline scrubbing and field-level diff highlighting**,
  Playwright-verified at the time. Re-implementing that from scratch for a
  "live" server would be pure duplication for zero new capability — the
  one thing tier 3 actually adds beyond tier 2 is the **object/ownership
  graph view**, which genuinely doesn't exist yet.

## Decision
`chronicle-cli serve <file> [--port N]` starts a local `cpp-httplib` server
(opt-in, `find_package(httplib CONFIG QUIET)`, same pattern as Zstd/Tracy/
EnTT elsewhere in this project) that **re-reads the `.chronicle` file fresh
on every request** rather than holding a live connection to a producer.
This is the honest, low-tech answer to "live sessions" available without a
real transport: point a teammate at a URL instead of a shared file, and
press the page's new **Refresh** button (re-fetches `/api/session`,
re-renders in place, preserves the current stream selection) after the
producer writes an updated export — no socket, no protocol, no new
producer-side code, verified to actually work (see below), not just
plausible in theory.

`GET /` and `GET /api/session` are both handled by `tools/cli/serve.cpp`,
which reuses `session_to_json()` (`html_export.cpp`, exposed via
`html_export.hpp` instead of staying file-local) — the *exact* JSON the
static export already embeds inline, now served instead. `viewer_script()`
(also extracted from the static export's page) is the *exact* same
stream-list/scrubber/replay/render JavaScript both pages run, verbatim —
`replayIndexed`/`replayKeyed`/`render()` are not reimplemented a third
time. The one substantive JS addition, `renderObjectGraph()`, groups
streams by name prefix up to the last `.` (`"player.health"` and
`"player.mana"` become fields of object `"player"`; a name with no `.` is
its own single-field object) — a derived view from `track()`'s and
`chronicle-adapter-entt`'s (`ADR 0015`'s `"component.field"`) own naming
convention, not a separate ownership model Chronicle tracks explicitly.
It's guarded by an `#object-graph` element check, so it's a harmless no-op
when included in the static export's page (which has no such element) —
one shared script for both pages rather than two near-duplicates.

**Deviation from docs/11's sketched layout**: [11-repository-structure-
and-standards.md](../11-repository-structure-and-standards.md) reserves a
separate `tools/viewer/` directory for "browser viewer static assets +
serve backend." This implementation lives in `tools/cli/` instead
(`serve.cpp`, `serve.hpp`, plus the `html_export.cpp` extensions above) —
splitting it into a second target would mean either duplicating
`session_to_json()`/`viewer_script()`/the session-loading logic
(`session_loader.hpp`, itself newly extracted from `main.cpp` so
`serve.cpp` doesn't duplicate `load_file()` either) into a second binary,
or introducing a shared library between the two targets for no benefit —
`chronicle-cli serve` is the same tool gaining one more subcommand, not a
separate program with a different process-separation story.

**Causal-chain graph** (docs/08's other tier-3 item) is *not* attempted
here: Chronicle's data model only knows "this stream's last mutation
happened at this call site" (`last_writer()`), not cross-field/cross-stream
dependency edges — there is no real graph to render beyond what the event
log's existing call-site column already shows per event. Scoped out
honestly rather than faked with a graph view that doesn't represent
anything Chronicle actually tracks.

### Verification performed
Real, not just "it compiled": started `chronicle-cli serve` against a real
exported session and drove it with Playwright (the same tool that verified
the static export in v0.2).
- `GET /` and `GET /api/session` both return correct content over real
  HTTP (`curl`-verified independently first).
- The Objects panel correctly grouped 3 streams into `player (2 fields)`
  and `match (1 field)`; clicking a field (`inventory`) selected the
  correct stream, correctly replayed its `IndexedOp` history
  (`["enchanted sword", "shield", "potion"]`), and rendered the correct
  event log with call sites.
- Dragging the scrubber correctly re-derived intermediate state
  (`["sword", "shield"]` at v1), confirming replay logic still works
  identically to the already-verified static export.
- **Refresh, the one genuinely new interactive behavior**: overwrote the
  underlying `.chronicle` file on disk with completely different content
  (a new `player.mana` stream) while the server kept running, clicked
  Refresh, and confirmed the page updated to the new stream/values/call
  site with no page reload — the concrete, working version of "how do you
  see updates without a live transport."
- Regression: full suite unaffected by the `html_export.cpp` refactor —
  **208/208 checks, 38 tests** (default, no `httplib`); **246/246 checks,
  50 tests** with Zstd/EnTT/httplib all available. The static export's
  generated page was re-checked (its DOM/CSS are byte-identical to before;
  only the shared JS gained the now-unused-there `renderObjectGraph`
  definition).

## Consequences
- Positive: the object/ownership graph view — the actual new capability
  this milestone was for — works, is verified against a real browser, and
  cost no duplicated rendering logic (shared with the static export).
- Positive: `chronicle-cli`'s dependency-free default build is unaffected;
  `serve` is opt-in exactly like Zstd/Tracy/EnTT, verified via the
  unchanged 208/208 default-build result.
- Positive: Refresh gives "live-ish" viewing a real, working answer today,
  without pretending a socket transport exists when it doesn't.
- Negative: not truly live — a producer must still periodically write out
  an updated `.chronicle` file (e.g. via `SessionWriter`) for Refresh to
  show anything new; there is no push, only re-read-on-demand.
- Negative: causal-chain graph is unimplemented — Chronicle's data model
  doesn't yet support anything beyond per-event call sites to build one
  from.
- Negative: deviates from docs/11's `tools/viewer/` sketch, for reasons
  specific to how much this feature reuses from `tools/cli/`'s existing
  code — recorded here rather than silently diverging from the documented
  layout.
- Follow-on: a real live transport (the same missing piece
  [ADR 0014](0014-storage-engine-compression.md) deferred LZ4 on) would let
  both this viewer and live-stream compression graduate from "re-read on
  demand" to genuinely live — one piece of infrastructure serving two
  deferred features, when built.
