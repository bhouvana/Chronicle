# ADR 0018: v1.0 API Stability Commitment — Scope and What It Explicitly Excludes

## Status
Accepted

## Context
[10-roadmap.md](../10-roadmap.md)'s v1.0 scope closes with "Stability
commitment: public API (Phase 7 surface) enters semver-stable status."
[07-api-design.md](../07-api-design.md)'s "Core surface" and "What we are
explicitly NOT putting in the core API" sections define that surface's
shape but never named a concrete versioning policy — this ADR is that
policy, written now that v1.0's other four items
([compression](0014-storage-engine-compression.md),
[the EnTT adapter](0015-entt-adapter.md),
[the interactive viewer](0016-interactive-browser-viewer.md), and
[the CI performance gate](0017-ci-performance-gate.md)) are shipped and the
surface they add is visible, not before it existed.

A real complication: this project's on-disk wire format has already bumped
**three times** (v1→v2 [ADR 0010](0010-call-site-capture.md), v2→v3
[ADR 0014](0014-storage-engine-compression.md)) under an explicit "no
compatibility requirement exists yet for this format" policy. A stability
commitment that silently also implied wire-format compatibility would
either be false the moment it was written, or would need to walk back
format evolution this project has already relied on twice. The commitment
below is scoped to avoid that contradiction directly, rather than leaving
it ambiguous.

## Decision

### What's covered: the umbrella header's surface
Everything reachable by `#include <chronicle/chronicle.hpp>` — the set
`docs/07` calls the Public API, concretely today: `tracked<T>`,
`tracked_vector<T>`, `tracked_map<K,V>`, `Session`/`Session::Config`,
`Stream<T>`/`StreamBase`, `Timeline<T>`, `Snapshot<T>`, `HistoryRecord<T>`/
`Event<T>`, `RetentionPolicy`/`OverflowPolicy`, `ContainerOp<T>`/
`ContainerOpKind`, `MapOp<K,V>`, the `track()`/`history()`/`snapshot()`/
`snapshot_at()`/`snapshot_at_version()`/`current_version()`/`last_writer()`/
`set()`/`diff()` free-function families for all three tracked types, and
`CHRONICLE_TRACK_TYPE`/`CHRONICLE_TRACKABLE`/`TrackedType<T,...>`/
`track_type()` ([ADR 0011](0011-tracked-type-explicit-handle.md)).
Follows semver: **MAJOR** for any source-breaking change (removing/renaming
a symbol, changing an existing signature's meaning, tightening a
precondition), **MINOR** for additive source-compatible changes (a new
method, a new opt-in header, a new adapter), **PATCH** for behavior-
preserving fixes. A symbol here surviving `docs/07`'s "API review bar" is
the entry condition for this guarantee, not a substitute for it — new
additions still have to clear that bar first.

### What's explicitly NOT covered
- **The on-disk wire format** (`include/chronicle/io/*.hpp`,
  `kFormatVersion`). Never pulled in by `chronicle.hpp`, already versioned
  independently, and staying that way: a `kFormatVersion` bump is not
  itself a semver-major bump of chronicle-core, and is not implied to
  respect any compatibility window by this ADR. A real cross-version
  compatibility commitment for the format is future work
  ([11-repository-structure-and-standards.md](../11-repository-structure-and-standards.md)'s
  own "CI/versioning story hasn't been reached" — still hasn't, for the
  format specifically).
- **Opt-in modules with a real external dependency**: `chronicle/
  tracy_bridge.hpp`, `chronicle/io/zstd_codec.hpp`, `adapters/entt/`,
  `tools/codegen`, and `chronicle-cli serve`'s `/api/session` JSON shape.
  These are real and tested (see their own ADRs' verification sections),
  but each is younger — some shipped this same release — than the v0.1-
  v0.5 core surface this commitment is built on evidence from. Best-effort,
  not semver-bound yet: a breaking change to one will be called out
  explicitly in its own ADR and release notes, the same transparency this
  project has already applied to every wire-format bump, rather than
  silently changing.
- **`chronicle-cli`'s exact command-line syntax** (subcommand names, flag
  spelling). Real and tested, but CLI ergonomics are the kind of surface
  that legitimately improves with use in ways a library API shouldn't —
  not frozen here.
- **Anything not reachable from `chronicle.hpp`** — `chronicle::detail::*`,
  `include/chronicle/ring_buffer.hpp`, and any header not in the umbrella's
  include list. Internal by construction; no compatibility promise at all.

## Consequences
- Positive: consumers of `tracked<T>`/`tracked_vector<T>`/
  `tracked_map<K,V>`/`Session`/`Stream<T>` and the free-function query API
  can depend on source compatibility across MINOR/PATCH releases starting
  now — the "no-compromise daily driver" framing
  [10-roadmap.md](../10-roadmap.md) gives v1.0 has a concrete, checkable
  meaning instead of being purely aspirational.
- Positive: the wire format keeps the freedom to evolve
  ([ADR 0014](0014-storage-engine-compression.md)'s LZ4/columnar-layout
  follow-ons, both still open) without that being read as a broken promise
  — the commitment was scoped to never claim that in the first place.
- Negative: newer opt-in surfaces (Tracy, Zstd, EnTT, the viewer's JSON
  API) get a weaker guarantee than the core — an honest reflection of how
  much less real-world exposure they've had, not a judgment that they're
  lower quality.
- Follow-on: revisit whether to fold the opt-in modules into the full
  semver commitment once each has shipped at least one release beyond the
  one that introduced it, giving them the same "survived a real release
  cycle" evidence the core surface has now.
