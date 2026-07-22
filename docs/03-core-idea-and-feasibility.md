# Core Idea, Feasibility & Risk

## The one capability nobody else has

> **Ask any tracked value "what were you, and why did you change" — as a live,
> in-process, programmatic query — without a debugger attached and without a
> full CPU-trace replay.**

Concretely: `chronicle::history(player.health)` returns an ordered list of
`{value, timestamp, thread, call-site, causal predecessor}` records you can iterate,
diff, filter, or render — while the program is *running*, from *inside* the program,
or from a saved session afterward. Every existing tool makes you choose: full
fidelity with no semantics (rr/TTD) or semantics with no history
(serializers/reflection). Chronicle's bet is that **bounded, opt-in, source-level
semantic history** is the missing middle, and that it's viable specifically *because*
it doesn't try to be a whole-machine deterministic replayer.

This is the idea everything else in the project must serve. If a proposed feature
doesn't make "query the history of a value" better, cheaper, or more expressive, it
doesn't belong in v1.

## Reframing: state history, not object history

Per the note at the end of the original brief: objects are one instance of a more
general primitive, **versioned state**. The core abstraction is:

```
StateStream<T> := ordered sequence of (Version, T-or-delta, Metadata)
```

An object's fields, a container's contents, an ECS world, an allocator arena, and a
custom simulation grid are all just different *producers* of a `StateStream`. This is
why the architecture (Phase 5) centers on a small recording/storage core with
*adapters* on top, rather than an "ObjectTracker" class with containers bolted on
later. Concretely this changes the API surface from `chronicle::track(Player&)`-only
to `chronicle::track(anything satisfying the Trackable concept)`, where `Player`,
`std::vector<Entity>`, and a `PhysicsWorld` all satisfy it via different adapters.

## Feasibility verdict, by claim

| Claim | Feasible in C++23 on Linux/Win/macOS? | Confidence |
|---|---|---|
| Intercept assignment to explicitly-registered fields | Yes — operator overloading / wrapper types (`chronicle::tracked<T>`) or macro-generated setters | High |
| Intercept assignment to *arbitrary, unmodified* struct fields with zero source changes | No, not portably — requires either DBI (Pin/DynamoRIO-class overhead) or a compiler fork; a Clang AST-driven codegen step can get close but requires a build-step, not zero-touch | Medium (with caveats — see Phase 4) |
| Track container mutation (push_back, erase, resize) | Yes — wrapper containers or allocator-level hooks | High |
| Track raw `memcpy`/`memmove` into tracked memory | No, not without either instrumenting libc calls (LD_PRELOAD/detours-style interposition, platform-specific) or DBI-level tracking | Medium — treat as a known, documented blind spot, optionally closed by an opt-in interposition shim |
| Deterministic replay across threads | Partially — requires either a deterministic scheduler (record thread interleaving, à la rr) or accept "the *values* replay correctly, ordering across threads is best-effort" | Medium — this is the single largest open research question, see [12-future-research-topics.md](12-future-research-topics.md) |
| Bounded, predictable overhead suitable for a 16ms game frame budget | Yes for the opt-in tracked-field/tracked-container model, at a cost proportional to what's tracked (this is a feature: you choose what history costs you) | High |
| Cross-platform (Linux/Windows/macOS) v1 | Yes — the core recording model is pure C++, no OS/kernel dependency; only the *optional* DBI/interposition extras are platform-specific | High |

## Primary technical risks, ranked

1. **Instrumentation coverage gap is not a bug, it's a permanent architectural
   property.** Chronicle will *never* honestly claim "every mutation is observed"
   (see Phase 4). The project succeeds or fails on whether "every mutation that goes
   through code you told us to watch" is still valuable enough — evidence from
   ASan (compiler-inserted hooks, not omniscient, still hugely valuable) says yes.
2. **Reflection gap forces manual registration**, which is real adoption friction
   (compare: Cereal, Boost.Describe both survive fine with manual registration, so
   this is a manageable, not fatal, risk) — mitigated by a codegen tool in v0.5+.
3. **Multithreaded determinism** is genuinely hard and is the most likely source of
   "this doesn't actually work like they said" complaints if oversold. Mitigation:
   be explicit in docs/marketing that v1-v1.x guarantee *per-object* causal ordering
   (happens-before via the mutation itself), not global deterministic replay.
4. **Storage growth**: naive full-history recording of a long-running process is
   unbounded. Mitigated architecturally by the Recording Model (Phase 6): deltas,
   ring-buffer retention windows, and explicit snapshot/eviction policy as first-class
   API, not an afterthought.
5. **"Yet another instrumentation macro library" perception risk** — the market is
   crowded with tools that ask you to annotate your code (Tracy zones, logging
   macros). Differentiator must be visible immediately: the *query and replay*
   experience, not the instrumentation call itself. This should drive documentation
   and demo priorities (Phase 8/10) — lead with `chronicle::diff(a, b)` and a replay
   scrubber, not with the tracking macro.

## Go/no-go

**Go**, scoped explicitly as: a semantic, opt-in, source-level state-history engine
for native C++, positioned as a complement to (not a replacement for) full CPU-trace
debuggers, targeting game/simulation/finance-style audiences first. The full
CPU-level deterministic replay ambition from the original brief is retained only as a
**long-term research direction** (Phase 12), not a v1-v2 commitment.
