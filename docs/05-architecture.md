# Architecture

## Design goals, in priority order

1. The core (recording + storage) has **zero knowledge of C++ types, containers, or
   game/domain concepts** — it only knows about opaque `StateStream`s of versioned
   byte-blobs-with-metadata. Everything type-aware lives in an **adapter layer** above
   it. This is the single most important boundary in the system (see Phase 3's
   "versioned state, not objects" reframing) and it is what lets the same core serve
   objects, containers, ECS worlds, and allocator arenas without special-casing any
   of them.
2. Recording is **always in the hot path** and must be independently
   benchmarkable/optimizable from everything else. Replay, diffing, visualization,
   and export are **always offline/cold path** relative to the recorded program.
   No subsystem above Recording is allowed to add overhead to it by being on the
   same thread, holding a lock it needs, or being a hard link-time dependency of the
   instrumented binary.
3. No cyclic dependencies between layers, ever — enforced by build target structure
   (Phase 11), not just convention.

## Layer diagram

```
                     ┌───────────────────────────────┐
                     │   Public API (chronicle::*)    │  header-only, what users touch
                     └───────────────┬─────────────────┘
                                     │
                     ┌───────────────▼─────────────────┐
                     │        Instrumentation           │  tracked<T>, macros, codegen
                     │  (Adapters: object / container /  │  hooks; ECS/allocator adapters
                     │   ECS / allocator / custom)        │
                     └───────────────┬─────────────────┘
                                     │ emits mutation events
                     ┌───────────────▼─────────────────┐
                     │        Recording Engine          │  hot path: lock-free capture,
                     │  (per-thread ring buffers,        │  minimal branching, no alloc
                     │   causal ordering, timestamps)    │  on steady-state path
                     └───────────────┬─────────────────┘
                                     │ flush / drain
                     ┌───────────────▼─────────────────┐
                     │         Storage Engine           │  cold path: snapshot+delta
                     │  (Snapshot / Delta / Compression) │  encoding, retention policy,
                     │                                    │  in-memory or on-disk backend
                     └──────┬───────────────┬────────────┘
                            │               │
              ┌─────────────▼───┐   ┌───────▼─────────────┐
              │  Replay Engine   │   │  Diagnostics /       │
              │  (deterministic  │   │  Query API           │
              │   value replay,  │   │  (history(), diff(), │
              │   scrubbing)     │   │   filter(), causal    │
              └─────────┬────────┘   │   chain queries)      │
                        │            └───────┬───────────────┘
              ┌─────────▼────────────────────▼───────────────┐
              │     Export / Visualization / CLI / Plugins     │  fully separate process
              │  (HTML export, terminal viewer, browser UI,     │  optional, never linked
              │   VS Code ext, Tracy/Perfetto bridge)            │  into the target binary
              └─────────────────────────────────────────────────┘
```

## Subsystem responsibilities & boundaries

- **Public API**: the only thing user code includes. Thin, stable, header-only
  facade. Never contains logic itself — delegates immediately to Instrumentation.
  This indirection is what lets us change the recording implementation without
  breaking user call sites (Phase 7 covers the surface itself).

- **Instrumentation**: owns the *only* code allowed to know about C++ type
  mechanics (operator overloading, construction/destruction hooks, container
  wrapping). Produces normalized `MutationEvent`s (opaque byte payload + type-id +
  field-id + object-handle + timestamp) and hands them to Recording. Adapters
  (object/container/ECS/allocator/custom) are plugins to this layer, each
  implementing one `Trackable`-family concept — this is where Phase 4's per-mechanism
  limitations are contained and documented, not leaked upward.

- **Recording Engine**: the only subsystem allowed to run *inside* the instrumented
  program's hot path. Per-thread lock-free ring buffers (precedent: Tracy), no heap
  allocation on the steady-state path, no mutexes, no I/O. Its only job is "get this
  event captured as cheaply as possible" — it does not decide retention policy,
  compress, or interpret the payload. Failure mode when the buffer is full is a
  policy decision exposed to the user (drop oldest / drop newest / block — never
  "silently corrupt"), not something Recording decides unilaterally.

- **Storage Engine**: owns the recording model (Phase 6) — turning a raw event
  stream into queryable, space-bounded history: snapshot cadence, delta encoding,
  compression, eviction/retention windows. Runs off the hot thread, either
  asynchronously in-process (a dedicated drain thread) or out-of-process (Tracy-style
  streaming to an external viewer). This is where "unbounded growth" (Phase 3's #4
  risk) is architecturally owned and solved, once, rather than left to each adapter.

- **Replay Engine**: reconstructs "what did this StateStream look like at version
  N / at time T," walking snapshots + deltas. Pure function of Storage's data —
  stateless with respect to the live program, so it can run against a live session
  *or* a session loaded from disk identically. This symmetry (live vs. saved session
  behind one API) is a hard requirement, not a nice-to-have — see Phase 7.

- **Diagnostics / Query API**: `history()`, `diff()`, `filter()`, causal-chain
  queries ("what wrote this value last," "what read/derived from this value").
  Built entirely on the Replay Engine's reconstruction primitive plus indices
  Storage maintains (by object, by type, by time range, by thread) — never reaches
  into Recording or Instrumentation directly.

- **Export / Visualization / CLI / Plugins**: strictly a *consumer* of the Query
  API, running as a separate process/tool, never linked into the target binary.
  This boundary is what keeps a game shipping build from ever paying for or
  depending on a GUI toolkit (see Phase 8's process-separation decision).

## Dependency rules (enforced, not aspirational)

- `chronicle-core` (Recording + Storage) depends on nothing but the C++ standard
  library. No adapter, no visualization dependency, ever leaks into it.
- `chronicle-instrumentation` depends on `chronicle-core` only.
- Adapters (`chronicle-adapter-stl`, `chronicle-adapter-entt`, ...) each depend on
  `chronicle-instrumentation` and their one target library (e.g. EnTT) — never on
  each other.
- `chronicle-replay` and `chronicle-query` depend on `chronicle-core` only (not on
  Instrumentation — replay/query operate on recorded data, not live C++ objects).
- Everything under `chronicle-tools/` (CLI, HTML export, viewer) depends on
  `chronicle-query`, never on `chronicle-core` internals directly, and is built and
  shippable as fully standalone binaries that can consume a session file with no
  headers from the instrumented project at all.

This mirrors the layering lesson from WinDbg TTD (Phase 1): keep capture dumb and
fast, keep analysis smart and separate, and never let the smart side's dependencies
(UI toolkits, compression libraries with GPL entanglements, etc.) touch the capture
side's link graph.

## What we rejected

- **A single monolithic `Chronicle` God object** owning tracking, storage, and
  querying — rejected because it's exactly what makes rr/UndoDB-style tools
  unembeddable as libraries; you get a debugger, not an API.
- **Reflection/codegen as a hard dependency of core** — rejected; core must work with
  zero codegen (manual registration), with codegen (Phase 4/7's Clang tool) as a
  productivity layer strictly on top, never required.
- **GUI/visualization linked into the target process** — rejected outright for the
  reasons in Phase 3 (game frame budgets) and Phase 5's boundary rules above; a
  "live view" instead streams data out to a separate viewer process, same as Tracy.
