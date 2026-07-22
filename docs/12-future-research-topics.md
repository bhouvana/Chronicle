# Future Research Topics

Explicitly open questions — not committed roadmap items (Phase 10), not solved
problems. Each is here because a real design decision upstream depends on its
eventual answer, or because it represents the project's long-term ceiling.

## 1. Deterministic multithreaded replay
The single largest gap between Chronicle's per-stream causal ordering (Phase 6) and
the full deterministic replay rr/UndoDB/WinDbg TTD offer (Phase 1). Candidate
approaches to investigate: (a) record a global happens-before graph via
lightweight vector clocks across tracked streams, accepting the overhead cost;
(b) integrate with a deterministic scheduler/record-replay layer (à la rr) as an
optional heavyweight mode layered *underneath* Chronicle rather than reimplemented
by it; (c) accept permanent best-effort status and invest instead in surfacing
*apparent* race conditions as flagged uncertainty in the UI rather than false
precision. No architectural commitment should be made that forecloses any of these
three.

## 2. C++26 static reflection (P2996) adoption path
Once compilers ship P2996, Chronicle's manual-registration and Clang-codegen paths
(Phase 4/7) could both be superseded by genuinely automatic field discovery.
Research question: can this be adopted as an *additional* registration backend
behind the existing `Trackable` concept without an API break, or does true automatic
discovery require rethinking the opt-in-per-field cost model (Phase 7) that
currently underpins the zero-cost guarantee? Track P2996's standardization status
and prototype against experimental compiler support as it becomes available.

## 3. Raw-memory interposition (closing the `memcpy`/DMA blind spot)
Phase 4 documents this as a permanent architectural blind spot for the core model.
Worth a dedicated research spike: how much of the gap can an *opt-in*, clearly
"heavyweight mode" libc-interposition shim (LD_PRELOAD on Linux, Detours on Windows)
close, at what overhead, and is the resulting complexity/platform-specificity
worth it for the security-research audience segment (Phase 2) that most wants it?

## 4. Formal cost model / static analysis for "what will tracking this cost me"
An IDE/compiler-time tool that estimates, from a tracked field's write frequency in
a profiled run, what its history storage footprint and hot-path cost will be —
turning Phase 9's performance philosophy from "measure after the fact" into
"predict before you commit to tracking this field." Related to existing static
cost-model work in the compile-time reflection/constexpr space.

## 5. Cross-run / cross-session diffing
Phase 2 identifies this as a killer feature for the simulation/robotics audience
("diff two runs of the same scenario"). Requires solving stream *alignment* across
sessions that don't share object identities/addresses (different process runs) —
likely needs a semantic alignment key (object name/role) rather than Chronicle's
existing generation-counted handle identity (Phase 4), which is inherently
single-run scoped. Open question: is this a Query API extension or a fundamentally
different tool built on exported session data.

## 6. Distributed / multi-process state history
Everything in this document set (Phase 5 onward) assumes a single process. Extending
`StateStream` semantics across a process boundary (e.g. a client-server game, a
microservice) is a substantial, separate research area — likely reusing the
event-sourcing framing (Phase 3) but requiring network transport, clock
synchronization beyond the hybrid logical clock upgrade already reserved in Phase 6,
and a materially different trust/security model. Not assumed to be in scope for this
project's identity at all; worth revisiting only after v2.0's ecosystem milestone
validates strong single-process adoption.

## 7. Embedded / allocation-free tier
Phase 2 concluded "mostly no" for constrained embedded targets. Worth periodically
re-evaluating: could a fixed-capacity, static-allocation-only Recording Engine
variant (no ring buffer growth, compile-time-sized streams) serve higher-end
automotive/industrial embedded use cases without compromising the core model's
generality for the primary game/sim/finance audience?

## 8. Machine-assisted anomaly detection over history
Once `StateStream`s exist as a queryable, structured corpus, there's a natural
follow-on: flag "this field's value is outside its historically observed range/rate
of change" automatically. Explicitly deferred past v2.0 — real risk of becoming
a research distraction from the core value proposition (Phase 3) before the
foundational query/replay experience is proven in the wild.
