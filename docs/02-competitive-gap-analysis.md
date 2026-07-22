# Competitive Analysis & Market Gap

## Positioning matrix

| Tool | Grain | Query model | Embeddable API | Cross-platform | Cost |
|---|---|---|---|---|---|
| rr | instruction stream | step through gdb | no | Linux only | free |
| UndoDB | instruction stream | timeline UI, reverse data bp | no | Linux only | $$$ |
| WinDbg TTD | instruction stream | offline SDK query | partial (C# SDK) | Windows only | free (MS) |
| Valgrind/Pin/DynamoRIO | memory access | custom tool code | yes (as a tool) | Linux mostly | free |
| ASan/MSan/UBSan | memory access (bugs only) | sanitizer report | no | all | free |
| Tracy | manual event zones | timeline UI | yes (macros) | all | free |
| Perfetto/ETW/Chrome trace | manual events/counters | timeline UI | yes | all | free |
| Cereal/Boost.Serialization | full-object snapshot | none (I/O only) | yes | all | free |
| EnTT/flecs observers | component mutation | signal callback | yes | all | free |
| **Chronicle (proposed)** | **named object/field history** | **programmatic + visual, in-process** | **yes, first-class** | **all (target)** | free/OSS |

## The gap, stated precisely

Every mature tool in this space picked one of two philosophies:

1. **"Trace everything, understand nothing automatically"** — rr, TTD, UndoDB,
   Pin/DynamoRIO. They achieve full-fidelity replay, but the recording is *opaque*:
   an instruction/memory trace with no notion of "this is a `Player`, this is its
   `health` field, here is when and why it changed." Turning that trace into an
   answer to "how did `health` reach -5?" requires a human driving a debugger UI,
   one step at a time, with no queryable API.

2. **"Understand one moment perfectly, forget the rest"** — Cereal, JSON
   serializers, ASan's crash report, a profiler's current-frame view. These tools
   know exactly what an object's *fields* are (via reflection/macros) but only at a
   single instant. There is no history, no diff, no "show me every version."

**Nobody occupies the middle**: a tool that knows your types semantically (field
names, not raw bytes) *and* keeps their history queryable and programmatic, embedded
in your own process, without requiring a kernel-level or JIT-recompilation harness.
That middle ground — semantic + historical + embeddable — is the gap.

## Would practitioners actually adopt this?

Evaluated per audience, honestly, including reasons to say no:

- **Game developers**: strong yes for gameplay/simulation state (desync bugs,
  "why did the AI do that three frames ago," replay-driven QA). Already comfortable
  with instrumentation macros (ECS observers, Tracy zones) — low adoption friction.
  Risk: hard real-time budgets (16.6ms/frame) make even "efficient" recording a hard
  sell without an aggressively low, provably-bounded overhead mode.
- **Simulation / robotics**: yes — deterministic replay of world/agent state is
  already a felt need (ROS bag files are a weaker version of exactly this, event logs
  without object semantics). Determinism and cross-run diffing (comparing two runs
  of the same scenario) is a killer feature here.
- **Finance (order books, risk engines)**: cautiously yes, but they will not accept
  unbounded memory/latency variance — this audience needs the "bounded overhead,
  bounded memory, deterministic worst case" story to be airtight and *measured*, not
  asserted. Also a strong pull toward "compliance-grade audit trail," which reframes
  Chronicle as much as a record-keeping tool as a debugging one.
- **Embedded**: mostly no on constrained targets (flash/RAM budgets, no heap for a
  history buffer) but plausible yes on higher-end embedded (automotive ECUs with
  megabytes to spare) if there's a tiny, allocation-free tier.
- **Compiler/kernel engineers**: no as primary users of this exact framing (they
  already have rr/gdb reverse-execution and don't want a library dependency in a
  compiler codebase), but the *approach* (Clang-based instrumentation) borrows
  directly from their tooling.
- **Security researchers**: partial yes for exploit/vuln analysis (reconstructing
  "how did this buffer's length field get corrupted") but they will likely still
  prefer full CPU-trace tools (rr, TTD) for anything adversarial, since Chronicle's
  compiler-inserted-hook model can be bypassed by exactly the kind of raw
  memory writes (memcpy, DMA, ROP) an attacker relies on — see
  [04-technical-limitations.md](04-technical-limitations.md).

**Verdict**: real, addressable demand exists, concentrated in game/sim/finance
audiences who already value observability and are the primary buyers of Tracy today.
This is a good size and shape of market for a v1 OSS project — not a universal
debugger replacement, and we should not market it as one.

## Explicit non-goals (to keep the project from collapsing under its own ambition)

- Not a security/adversarial-hardened trace (assume cooperative code, not attacker
  code, for v1-v2).
- Not a replacement for rr/TTD's whole-program deterministic CPU replay — we
  complement it (semantic layer) rather than compete with it (fidelity layer).
- Not a general profiler (no CPU sampling, no flame graphs of call stacks) —
  Tracy/Perfetto already do this well; we integrate rather than duplicate
  (see [08-visualization.md](08-visualization.md)).
