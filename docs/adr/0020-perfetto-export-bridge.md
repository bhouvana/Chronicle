# ADR 0020: Perfetto Export Uses the Chrome JSON Trace Format, Not Protobuf

## Status
Accepted

## Context
[10-roadmap.md](../10-roadmap.md)'s v2.0 scope calls for a Perfetto export
bridge. [08-visualization.md](../08-visualization.md)'s "Interop over
reinvention" section frames this exactly like the Tracy bridge
([ADR 0013](0013-tracy-bridge.md)) before it: "reusing Perfetto's mature
timeline UI for users who prefer it over the native HTML viewer," not
Chronicle building a second competing timeline view.

Perfetto has two ways to accept a trace: its native protobuf-based `Trace`/
`TracePacket` format (the one Perfetto's own documentation focuses almost
entirely on), or the legacy Chrome JSON Trace Event Format inherited from
`chrome://tracing`. Verified directly against Perfetto's own current
documentation and the Chrome Trace Event Format spec (fetched live, not
assumed from training data, since a spec choice this load-bearing deserves
a real check): `ui.perfetto.dev` loads Chrome JSON traces natively — "Chrome
JSON files can be opened directly in the Perfetto UI" — including a **bare
JSON array of event objects** as the simplest accepted shape, no
`{"traceEvents": [...]}` wrapper required. Choosing this format over
protobuf avoids a real dependency cost for a comparatively rare export
path: protobuf serialization would need either a code-generated schema and
the protobuf runtime library, or a hand-rolled wire encoder for a format
Perfetto itself describes primarily through a Python-based tool, neither of
which this export path's value justifies. `tools/cli/perfetto_export.cpp`
needs nothing beyond what `html_export.cpp` already uses (plain JSON text
generation) — chronicle-cli remains buildable with zero new dependency for
this feature specifically, the same "no dependency where none is needed"
discipline this project applies everywhere else.

## Decision
`chronicle-cli export --perfetto <file> <output.json>` maps each stream
shape onto the Chrome format's two event kinds that actually fit the data,
rather than forcing everything into one:
- **Scalar streams with a numeric `WireValue`** (`Int64`/`UInt64`/`Double`/
  `Bool`) become **Counter events** (`"ph":"C"`) — verified directly that
  Perfetto renders these as value-over-time tracks, exactly matching what a
  `tracked<T>` scalar field's history actually is. A scalar `String` stream
  has no numeric counter representation and falls back to an Instant event
  carrying the value in `args` — an honest fallback, not a fabricated
  numeric encoding of text.
- **`IndexedOp`/`KeyedOp` streams** (structural container changes) become
  **Instant events** (`"ph":"I"`) per event, carrying `op`/`key`/`value` in
  `args`. A structural change has no natural continuous-value
  representation, so treating it as a plotted line would imply continuity
  the data doesn't have — an annotation marker is the honest mapping.
- **`tid` is each event's real `thread_hash`** (`chronicle/io/format.hpp`'s
  existing field, already captured for every event) — real, derived data,
  not fabricated. **`pid` is a fixed synthetic value** for the whole
  exported session, since Chronicle's data model has never captured an OS
  process id anywhere; inventing a more specific one would be fabricated
  rather than derived.
- `ts` is converted from Chronicle's nanosecond-precision `elapsed_ns` to
  the microseconds the Chrome format requires — a real, one-way precision
  loss on export (nobody re-imports a Perfetto trace back into Chronicle,
  so this isn't a round-trip concern).

### Verification performed
Not just "the JSON is well-formed": the exported trace was loaded into the
**real, live Perfetto UI** (`ui.perfetto.dev`) via Playwright — the same
verification standard this project already held the HTML export and
interactive viewer to
([ADR 0016](0016-interactive-browser-viewer.md)). Uploaded
`demo_perfetto.json` (from `examples/export/`'s 3-stream demo session)
through the real "Open trace file" file picker, and confirmed via a
screenshot, not just a DOM snapshot: **`player.health`'s counter track
renders as a real step chart, visibly dropping from 100 to -5** (matching
the exact recorded sequence `100 → 75 → 45 → -5`), and **`player.inventory`/
`match.scores`'s structural changes render as purple instant-event
markers** under "Process 1 → Thread 1." The page title itself
(`demo_perfetto.json (1 MB) - Perfetto UI`) and duration readout
(`205µs`, matching the session's actual elapsed time) confirm Perfetto's
own trace processor genuinely parsed the file, not just accepted arbitrary
bytes.

## Consequences
- Positive: a real Perfetto trace, verified in the actual Perfetto UI —
  the concrete deliverable this roadmap item asked for, not just a
  plausible-looking JSON file.
- Positive: zero new dependency for `chronicle-cli` — this export path
  reuses the same plain-JSON-text-generation approach `html_export.cpp`
  already established, unlike Zstd/Tracy/EnTT/httplib, all of which needed
  a real external library.
- Negative: nanosecond timestamp precision is lost on export (truncated to
  microseconds) — an inherent, one-way limitation of the target format,
  not something a different implementation choice here could avoid.
- Negative: structural container changes (vector/map ops) only get instant-
  event annotations, not a richer visualization (e.g. per-element counter
  tracks) — a reasonable, honest scope for a first pass; a real follow-up
  if that granularity turns out to matter in practice.
