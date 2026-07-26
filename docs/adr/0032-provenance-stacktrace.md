# ADR 0032: Full Call-Chain Provenance via `std::stacktrace`

## Status
Accepted

## Context
[docs/13-vision.md](../13-vision.md)'s Layer 3 asks for a full causal chain
(`ApplyDamage() ← Projectile::Hit() ← Physics() ← UpdateFrame()`), beyond
the single frame `last_writer()`/`call_site` ([ADR 0010](0010-call-site-capture.md))
already give. Three facts were verified directly before any design
decision, not assumed:

1. **`std::stacktrace` (C++23) works, with real symbol/file/line
   resolution**, on both compilers this project's CI matrix actually has
   — MSVC 19.44 and Clang 21.1.6 — confirmed by compiling and running real
   probes, given debug info (`/Zi` on MSVC, `-g` on Clang/GCC). Without
   debug info, only module+offset is available.
2. **Real, measured cost, in full context**: `bench/main.cpp`'s
   `set_with_stacktrace` benchmark reports **116,787.63 ns/op**
   (`bench/baseline.json`) — against 63-91 ns/op for a plain tracked
   write, roughly **1,300-1,850x**. This includes eager symbol resolution
   (`entry.description()`/`source_file()` called for every frame, not
   deferred), the realistic cost of actually using the feature. This is
   three orders of magnitude past [ADR 0019](0019-hybrid-logical-clock.md)'s
   HLC cost (~30-50%), the most expensive existing opt-in feature before
   this one.
3. **Optimizers can inline whole frames away**: an early probe showed two
   trivial one-line wrapper functions vanish entirely from a captured
   trace under `/O2`. A trace reflects the compiled binary's actual call
   graph, not a guaranteed map to every function the source defines.

## Decision
`include/chronicle/provenance.hpp`:
`chronicle::set_with_stacktrace(tracked<T>& field, T value, call_site = current())` —
captures `std::stacktrace::current()`, converts every frame immediately to
a storable `StackFrame{description, source_file, source_line}` (never
holding onto the platform-specific entry/handle), records the value
through the existing `field.assign()` (the same mechanism `chronicle::set()`
uses), and stores the trace in `chronicle::provenance::Registry`, keyed by
`(stream_id, version)`.

**Deliberately a separate, differently-named function, never an overload
of `set()`** — given fact 2 above, silently attaching this cost to
anything that looks like the existing cheap `set()` would be dishonest.
**Deliberately not a `Session::Config` flag** the way `causal_clock` is —
at three orders of magnitude past HLC's cost, this cannot be "one branch
when off" the way every other opt-in feature in this project is; it must
be an explicit call at each site the caller actually wants it, not a
session-wide toggle.

`StreamBase` gains one new accessor, `id()`, returning `Stream<T>`'s
already-existing, already-monotonic `id_` (created for the ring-buffer
cache in [ADR 0009](0009-lock-free-ring-buffer.md) specifically to survive
stack-address reuse across sequential `Stream<T>` objects). The provenance
`Registry` keys on this instead of a raw `Stream<T>*`, reusing an
already-proven identity scheme rather than risking the exact bug class
ADR 0009 already found once.

**In-process only for this increment** — not persisted to the `.chronicle`
wire format. That is real, separate future scope: it would need its own
format bump and a real decision about storing variable-length per-event
data (a multi-frame trace, each frame with a `description`/`source_file`
string) far larger than [ADR 0024](0024-cost-model-tool.md)'s ~81
bytes/event scalar baseline. No `chronicle-cli` surface exists yet for the
same reason — there's nothing in a `.chronicle` file to show.

### Verification performed
`tests/unit/provenance_test.cpp` (4 tests, built with `/Zi`/`-g` so real
symbol resolution is actually exercised, not degenerate module+offset):
querying before any capture returns `nullopt`; a real capture through two
`noinline`-guarded helper functions returns a trace with real
`source_line` info; querying an unrecorded version returns `nullopt`; and
a cross-contamination test using two fields that deliberately land on
colliding version numbers. That last test caught a real design mistake
while writing it: comparing the two traces' *top* frame doesn't
disambiguate them, since `capture_current_stacktrace()`/
`set_with_stacktrace<T>()` are the same instantiated frames regardless of
caller — the actual signal is frame *count* (the extra real call frames
the `noinline` helper chain adds). Fixed before landing, not left
subtly wrong. Full suite: **362/362 checks across 93 tests.**

Cost folded into the real benchmark suite (`bench/main.cpp`,
`bench/baseline.json`, `bench/RESULTS.md`) rather than left as a one-off
scratch number — see fact 2 above.

## Consequences
- Positive: a real, working answer to Layer 3, using only standard C++23
  library facilities (no new external dependency, unlike topic 3's
  Detours-based interposition) — verified on both compilers this
  project's CI matrix has.
- Positive: the cost is now a tracked, real benchmark number, not a claim
  — anyone considering this feature sees the actual order-of-magnitude
  trade-off before opting in.
- Positive: zero cost for anyone who never calls `set_with_stacktrace()` —
  additive to the umbrella header, no change to `record()`'s hot path.
- Negative: in-process only; a trace captured during one run is gone once
  the process exits. Persisting it is real, unattempted future scope.
- Negative: inherited, documented limitation — optimized builds can hide
  real source-level calls that got inlined away; the trace is honest about
  the compiled binary, not necessarily about every function the developer
  wrote.
- Negative: requires debug info (`/Zi`/`-g`) to be useful at all — a real
  deployment consideration (many shops already ship PDBs for exactly this
  kind of post-mortem capability, but it's not free by default).
