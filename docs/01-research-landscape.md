# Research Landscape: Prior Art Survey

This document surveys the existing tools and research areas adjacent to "Chronicle" —
a runtime state history / time-travel engine for native (C++) programs. The goal is
not breadth for its own sake: each entry ends with **why it matters to us** and
**where it stops short of what we want to build**.

## 1. Reverse / Time-Travel Debuggers

### rr (Mozilla)
Record-and-replay debugger for Linux. Records a *deterministic execution trace* at
the syscall/scheduling level (not memory-diff level) using ptrace, then replays the
exact instruction stream so gdb can step backward. Extremely low overhead recording
(~1.2x) because it doesn't track individual memory writes — it replays computation,
not state.
- **Relevant**: proves whole-program deterministic replay is viable at near-native
  speed on Linux/x86-64.
- **Falls short for us**: it reconstructs state by *re-executution*, not by storing
  object history. You can't ask "show me every value `Foo::bar` held" without manually
  stepping through the whole replay. No object-level or semantic query surface.
  Linux/x86-64 only (perf-counter-based retiming), no Windows/macOS.

### UndoDB / Undo LiveRecorder
Commercial reverse debugger for Linux, similar space to rr but with proprietary
recording (JIT-instrumented, not ptrace-based), broader platform/compiler support,
and a polished timeline UI ("Time Travel" scrubber, causality/data breakpoints —
"tell me when this memory location was last written").
- **Relevant**: "reverse data breakpoint" (last-write-to-address) is exactly the
  primitive users want from us, and proves there's commercial demand for it.
- **Falls short for us**: closed-source, expensive, still fundamentally a debugger
  session artifact (a recording tied to one run, inspected through a debugger UI),
  not a library you embed and query programmatically, not exportable/diffable,
  no notion of "object identity across the run" as a first-class API.

### WinDbg Time Travel Debugging (TTD)
Microsoft's record/replay for Windows, similar niche to rr/UndoDB — CPU-level trace,
replay through WinDbg with "go back", data breakpoints, call tree, "!tt" indexing.
Uses a bit-accurate instruction-level trace format (`.run` files) which can be huge
(GB/minute) but is fully queryable offline (`ttd` analysis SDK) after capture.
- **Relevant**: proves a *file format for a full-fidelity trace* plus a *separate
  offline analysis SDK* is a workable split — recording stays dumb and fast, analysis
  stays smart and slow. This maps directly onto our Recording Engine / Replay Engine
  split (Phase 5).
- **Falls short for us**: Windows/x86 only, proprietary trace format, no source-level
  "track this struct" instrumentation — it's a CPU trace, not an object model.

### GDB / LLDB reverse execution
Both support `reverse-step`/`reverse-continue` via either full re-execution replay
(record full / `process record`) or checkpoint+re-run heuristics. Overheads are large
(10-50x for `record full`) because they log every register/memory write.
- **Relevant**: confirms naive "log every write" is too slow for anything but short
  debug sessions — validates our need for smarter recording strategies (Phase 6).

## 2. Dynamic Binary Instrumentation

### Intel Pin, DynamoRIO, Valgrind
JIT-recompile the target binary to insert instrumentation at the instruction level.
Can intercept every memory access, making full write-logging possible without source
changes.
- **Relevant**: this is the *only* mechanism that can honestly claim "every mutation
  is observed," which several downstream design questions (Phase 4) depend on.
  Could be an optional, opt-in, heavyweight "verify mode" or "capture mode" backend.
- **Falls short for us**: 5-100x slowdown, Linux-centric (DynamoRIO/Pin have Windows
  ports but weaker), and — critically — instruction-level traces have no idea what a
  "field" or "object" is; you'd still need DWARF/PDB type info layered on top to turn
  raw address writes back into `Player::health = 42`.

### AddressSanitizer / MemorySanitizer / UndefinedBehaviorSanitizer
Compile-time instrumentation (LLVM passes) inserted by the compiler itself, not a
separate JIT layer. ASan shows that *compiler-inserted instrumentation* at load/store
sites is practical in production-adjacent builds (2x overhead), and that shadow-memory
techniques scale to full-program memory tracking.
- **Relevant**: strongest architectural precedent for our leading strategy — a Clang
  plugin / compiler-assisted instrumentation pass that inserts hooks at assignment and
  construction points, rather than trapping at the OS/CPU level. Shadow memory is also
  a candidate mechanism for the Memory Tracker subsystem (Phase 5).

## 3. Tracing & Profiling Systems

### Tracy, Perfetto, Chrome Tracing (`chrome://tracing`), ETW
Event-based, high-frequency, low-overhead tracing frameworks built around: ring
buffers, lock-free per-thread queues, lazy/deferred string interning, and a
zoomable timeline UI (flame graphs, lane views).
- **Relevant**: this *is* our closest UX cousin. Tracy in particular (C++, header-only
  client, ~microsecond overhead per zone, live streaming to a separate viewer process)
  is close to a reference implementation for the Recording Engine's performance
  envelope and for a "live view while the program runs" mode.
- **Falls short for us**: they trace *events* (spans, counters, plots) that are
  manually annotated by the developer; they have no concept of *object state* or
  *history of a value*, and no replay/diff semantics.

## 4. Reflection, Serialization, and ECS

### C++ reflection (P2996 static reflection, `__builtin_dump_struct`, RTTR, Boost.PFR)
No stable compiler-portable runtime reflection exists in C++23. P2996 (static
reflection via `std::meta`) is targeting a future standard (not guaranteed in the
window we're planning around); Boost.PFR/aggregate-based "poor man's reflection"
works only for aggregates without custom logic.
- **Relevant**: reflection is a hard dependency for "automatically know what fields an
  object has." Its absence is the single biggest reason this problem has stayed
  unsolved as a general library rather than a per-project macro system.
- **Falls short for us**: means Chronicle *cannot* be fully automatic in C++23; it
  needs either (a) explicit per-type registration macros (`CHRONICLE_TRACK(Player,
  health, position)`, precedent: Boost.Describe, Cereal, nlohmann::json's
  `NLOHMANN_DEFINE_TYPE_*`), or (b) a Clang-based codegen step that parses the AST and
  emits registration code, or (c) both, with (a) as the always-available baseline and
  (b) as an opt-in accelerant.

### Entity-Component-System frameworks (EnTT, flecs)
Store component data in tightly packed arrays and already track structural changes
(component add/remove) via signals/observers.
- **Relevant**: ECS worlds are a natural high-value adapter target ("replay this
  simulation frame by frame") and their existing observer hooks are a cheap
  integration point — we don't need our own instrumentation in an EnTT-based game,
  just a Chronicle adapter subscribing to EnTT's `on_construct`/`on_update`/`on_destroy`
  signals.

### Event sourcing / CQRS (application-level, not native-object-level)
Standard backend pattern: store a log of *events*, derive current state by folding.
- **Relevant**: this is the correct *conceptual model* for Chronicle's recording
  format — an append-only, ordered, replayable log of mutations, with materialized
  snapshots as a performance optimization. It reframes "track this object" as "emit
  events when this object changes," which is a good API-design anchor (Phase 7).

### Persistent / immutable data structures (Okasaki), copy-on-write, hash-consing
Structural sharing techniques (as in Clojure's persistent vectors/maps, Immer for
C++) let you keep every historical version of a structure cheaply because unchanged
subtrees are shared, not copied.
- **Relevant**: candidate representation for the *in-memory* history of tracked
  containers — `chronicle::vector<T>` could be backed by a persistent tree so
  "give me the state as of version N" is O(log n) instead of O(n) full copies.
  Immer (Sinusoidal Software) is a mature C++17 implementation to study directly.

## 5. Debug Info & Compiler Tooling

### DWARF / PDB, Clang LibTooling / AST Matchers, LLVM passes
DWARF (Linux/macOS) and PDB (Windows) already encode struct layout, field
offsets/names, and type relationships — everything reflection would give us, just
external to the binary and keyed by address instead of by live pointer.
- **Relevant**: a *zero-instrumentation* fallback mode is plausible: attach to a
  running/core-dumped process, walk DWARF/PDB type info against known memory
  addresses, and reconstruct "what does this object look like" without any source
  changes — closer to what UndoDB/WinDbg TTD do. Good complement to the compiled-in
  instrumentation mode, not a replacement (loses "why did it change," only gets
  "what does it look like now").
- Clang LibTooling gives us a supported path to a source-to-source or AST-driven
  registration generator (see ECS/reflection section above) without forking Clang.

## 6. What We Are Deliberately Not Re-Studying Deeply

Full CPU-level record/replay (rr/TTD-class engines) is a 5+ year, OS-kernel-adjacent
undertaking on its own and is **not** the core bet of this project (see
[03-core-idea-and-feasibility.md](03-core-idea-and-feasibility.md)). We treat it as
prior art to learn UX and format lessons from, not infrastructure to rebuild.
