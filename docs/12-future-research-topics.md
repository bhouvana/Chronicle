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

### v2.0 research spike findings (docs/10-roadmap.md's v2.0 item — evaluated, not
### pre-committed, per that item's own framing)

This spike is grounded in direct, first-hand evidence from work already shipped
this cycle, not speculation — each candidate below is assessed against something
this project actually built and measured, not a hypothetical.

**(a) Global happens-before graph via lightweight clocks — partially built, and
the evidence argues against going further.** [ADR 0019](adr/0019-hybrid-logical-clock.md)
implemented exactly this idea, deliberately scoped down to the smallest useful
slice: a single, opt-in, *pairwise*-comparable ordinal shared across a
`Session`'s streams — not a full vector clock, not a happens-before graph, no
cross-thread causal claims at all beyond "these two timestamps have a total
order." Even that minimal slice cost a real, measured **~30-50% per-event
overhead** when enabled (`bench/RESULTS.md`), and needed a genuine correctness
fix mid-implementation (`snapshot_at_hlc()`'s early-break-loop bug, found by
reasoning through a concurrent interleaving, not by inspection) to avoid
silently wrong answers under real contention. A *full* vector-clock graph —
one entry per thread, updated and compared across arbitrary numbers of
streams, with actual happens-before edges instead of one flat ordinal — would
multiply both costs substantially: more per-event bookkeeping, more state to
keep coherent under concurrent writers, and a query surface (walking a graph,
not comparing two integers) with materially more surface area for the same
class of interleaving bug ADR 0009 already found twice in something far
simpler (a ring buffer) and ADR 0019 found once more in the HLC's own query
path. **Not recommended as a v3 commitment**: the cost curve from the one
data point this project actually has scales the wrong way for the marginal
value — Chronicle's actual users (docs/02's audience) query "what changed
and why," which the existing per-stream ordering plus the HLC's cross-stream
ordinal already answers for the overwhelming majority of real questions;
a full causal graph mainly helps answer "was there a race here," which
candidate (c) below can address more cheaply.

**(b) rr-style deterministic scheduler underneath Chronicle — not attempted,
and the reason is architectural, not effort.** This isn't a "haven't gotten
to it yet" gap: it conflicts with a foundational choice this project made
before v0.1 shipped. [03-core-idea-and-feasibility.md](03-core-idea-and-feasibility.md)
and [04-technical-limitations.md](04-technical-limitations.md) both concluded
that raw-memory/DBI-level instrumentation (the same *class* of mechanism
rr's deterministic replay needs — ptrace-based syscall interception,
instruction-level determinism) is explicitly out of Chronicle's model:
Chronicle instruments at the *source/API* level (`tracked<T>`, adapters),
never below it. Layering rr-class determinism "underneath" would mean
either depending on rr itself (Linux-only, a large, separate, mature
project this codebase has no reason to absorb) or reimplementing a
meaningful fraction of it — a different project's worth of scope, not a
Chronicle feature. **Not recommended**: this remains correctly parked as
"a separate tool that could be used *alongside* Chronicle," per docs/02's
original competitive positioning, not something Chronicle itself should
build.

**(c) Permanent best-effort status + flag apparent races — the recommended
path, and now cheaper to build than it was when this document was first
written.** [ADR 0003](adr/0003-causal-not-global-ordering.md) already
committed to best-effort cross-stream ordering as the permanent model, not
a stepping stone — this spike's findings support keeping that, not
revisiting it. What's new since ADR 0003: the HLC (ADR 0019) that now
exists specifically *because* this spike needed a real primitive to
evaluate (a) against gives (c) a concrete, low-cost mechanism it didn't
have before — two events on different threads whose HLCs are close
together with no established same-thread program-order relationship are
exactly the shape of "apparent race" (c) called for flagging, and the
`HlcTimestamp` comparison this already needs is the *same* comparison
`snapshot_at_hlc()` already does, not new machinery. **Concrete, real
follow-up — now shipped, per [ADR 0023](adr/0023-possible-race-query.md).**
`chronicle::possible_race(event_a, event_b, window_us = 0)` returns true when
two events' HLCs are within a configurable window and their threads differ,
scoped honestly as "may have raced" — this project's source-level
instrumentation has no synchronization information to claim more. Verified
with real racing threads, not simulated timestamps —
`tests/unit/race_test.cpp`, 264/264 checks across 58 tests. Not a reopening
of the "no v3 commitment" conclusion below: it's the one narrowly-scoped
primitive candidate (c) called for, built entirely on machinery (the HLC)
that already existed, with no new cost and no new wire-format version. Still
not usable across different `Session`s/processes — see topics 5 and 6.

**Bottom line**: no v3 commitment to full deterministic replay. The
evidence this project has actually collected — two real concurrency bugs
found the hard way in the ring buffer, a correctness bug found while
building even the smallest useful slice of approach (a), and a foundational
architectural conflict with approach (b) — argues for staying with (c),
now backed by a genuine, working, but *narrow* primitive (the HLC) rather
than pursuing the two approaches that would require an order of magnitude
more complexity for audiences (docs/02) this project isn't primarily built
for.

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

### Post-v2.0 spike findings (evaluated, not pre-committed — same framing as the
### deterministic-replay spike above)

This spike answers "does a `memcpy` hook even work" with a real, running
Detours-based proof of concept on Windows (`Microsoft::Detours`, available
via vcpkg — no new permanent dependency, evaluated the same opt-in way
Tracy/Zstd/EnTT were), not just a plausibility argument — and the answer
is a qualified, materially incomplete yes.

**Compile-time-constant sizes — the common `tracked<T>` scalar case — are
structurally invisible to any libc-level hook, confirmed at both the
assembly and runtime level, not assumed.** A `memcpy(dst, src,
sizeof(Small4))` (a 4-byte `int`-sized struct) compiled under MSVC `/O2`
produces **zero reference to the `memcpy` symbol at all** — the optimizer
inlines it directly into a single `mov eax, [rdx] / mov [rcx], eax` pair.
This holds even at 64 bytes (`movups`-based inlining, still no `call`).
A real Detours hook attached to `memcpy` and exercised against exactly
this code path fired **zero times** — not a theoretical gap, a measured
one. Since a raw `memcpy` directly overwriting a `tracked<int>`/
`tracked<double>`'s backing storage (the exact scenario this topic exists
to catch — a stray buffer overrun or an errant `reinterpret_cast`-based
bulk copy landing on a tracked scalar) is usually a small, fixed-size
copy, the case interposition was meant to close for the *most common*
tracked-field kind is precisely the case it cannot close.

**A genuinely runtime-variable size does survive as a real, hookable
call — confirmed both statically and at runtime.** The same function
compiled with a size parameter the optimizer cannot constant-fold (e.g.
derived from `argc`, matching a real buffer/network-copy shape) emits
`EXTRN memcpy:PROC` and a tail-call `jmp memcpy` through the imported
symbol; the same Detours hook fired correctly, reporting the exact
destination address and size. This is the scenario interposition
*would* actually help with: bulk copies into a `tracked_vector<T>`'s
backing array from an external buffer, which is plausibly a real
security-research use case (Phase 2) — but it is not the scalar-field
case that motivated this topic in the first place.

**A second, unprompted hook firing was also observed** — an internal
CRT/stdio call (a fixed ~10-byte copy at a static address, appearing
consistently regardless of program input) triggered the same hook. A
process-wide `memcpy` interposition catches *every* call in the process,
not just ones touching Chronicle-tracked memory — a real deployment
would need to filter hook firings against the address ranges of live
`tracked<T>`/`tracked_vector<T>`/`tracked_map<K,V>` instances (Chronicle
already does address-based bookkeeping for exactly this purpose in the
PMR allocator adapter, [ADR 0021](adr/0021-pmr-allocator-adapter.md), so
the mechanism exists — but it would need to be threaded through this
different call path, real additional scope, not attempted in this spike)
to be usable at all rather than an unfiltered, mostly-irrelevant firehose.

**Not recommended as a near-term commitment.** The core problem: the
interposition mechanism's blind spot (compile-time-constant sizes) and
its coverage (runtime-variable sizes) partition almost exactly along
"the common case" vs. "the uncommon case" for `tracked<T>` scalars
specifically — the audience most likely to want this (Phase 2's
security-research segment, worried about a stray write landing on a
tracked field) would get real coverage for large/bulk copies and **no**
coverage for the small, fixed-size copies that are arguably the more
frequent accidental-corruption shape in practice. Combined with the
address-range-filtering work still needed to make it usable rather than
noisy, and the platform-specific implementation this would require twice
over (Detours on Windows, LD_PRELOAD on Linux, with genuinely different
mechanics — this spike only built and ran the Windows side), the
cost/value ratio reads as unfavorable versus this project's existing
documented, honest blind-spot statement in
[04-technical-limitations.md](04-technical-limitations.md). Left
findings-only, matching this document's own "not a solved problem"
framing — no ADR, no shipped code, no opt-in build flag added.

## 4. Formal cost model / static analysis for "what will tracking this cost me"
An IDE/compiler-time tool that estimates, from a tracked field's write frequency in
a profiled run, what its history storage footprint and hot-path cost will be —
turning Phase 9's performance philosophy from "measure after the fact" into
"predict before you commit to tracking this field." Related to existing static
cost-model work in the compile-time reflection/constexpr space.

**Shipped — see [ADR 0024](adr/0024-cost-model-tool.md).** `bench/cost_model.py`
projects hot-path cost and on-disk storage footprint for a `tracked<T>`
scalar field from real `bench/baseline.json` numbers plus a directly
measured on-disk bytes/event constant, given an expected write frequency,
thread count, causal-clock setting, and retention policy. Refuses to
estimate configurations `chronicle-bench` has no real measurement for
(e.g. contended + causal_clock) rather than guessing. Scalar-only for
now — `tracked_vector`/`tracked_map`'s variable-size payloads need their
own measurement methodology, left as real future scope.

## 5. Cross-run / cross-session diffing
Phase 2 identifies this as a killer feature for the simulation/robotics audience
("diff two runs of the same scenario"). Requires solving stream *alignment* across
sessions that don't share object identities/addresses (different process runs) —
likely needs a semantic alignment key (object name/role) rather than Chronicle's
existing generation-counted handle identity (Phase 4), which is inherently
single-run scoped. Open question: is this a Query API extension or a fundamentally
different tool built on exported session data.

**Shipped — see [ADR 0025](adr/0025-cross-run-diffing.md).** Answered the open
question: a different tool, not a Query API extension —
`chronicle-cli diff-runs <file-a> <file-b>` aligns streams by name (the
semantic key this topic called for), diffs scalar streams by ordinal
position within each run's history, and container streams by final
replayed state only (full op-by-op alignment would report spurious
differences for equivalent-but-differently-ordered op sequences). Verified
against two real, separately-produced `.chronicle` files with a seeded
divergence.

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

**Shipped — see [ADR 0027](adr/0027-embedded-tier.md).**
`chronicle::embedded::TrackedScalar<T, Capacity>`: a genuinely separate,
opt-in module (not reachable from the umbrella header), fixed-capacity
circular history over `std::array`, no heap allocation, no thread safety
(single-core/cooperative-scheduling audience, matching the
allocation-free budget this topic asks for). The zero-heap-allocation
claim is verified directly, not assumed — which surfaced a real, reusable
gotcha in this project's own test framework along the way (see the ADR).

## 8. Machine-assisted anomaly detection over history
Once `StateStream`s exist as a queryable, structured corpus, there's a natural
follow-on: flag "this field's value is outside its historically observed range/rate
of change" automatically. Explicitly deferred past v2.0 — real risk of becoming
a research distraction from the core value proposition (Phase 3) before the
foundational query/replay experience is proven in the wild.

**Shipped — see [ADR 0026](adr/0026-anomaly-detection.md).**
`chronicle::range_anomalies()` scores a `tracked<T>` scalar's history
against its own causal (past-only, never hindsight) running mean/stddev via
Welford's algorithm — real statistical scoring, explicitly not a trained ML
model, stated plainly since "machine-assisted" could otherwise overclaim.
Scalar-only, same scoping precedent as the cost-model tool (topic 4).
Verified including a test that specifically exercises the causal design
choice (an early outlier followed by 30 normal values is still flagged,
which a whole-history batch z-score could have washed out).
