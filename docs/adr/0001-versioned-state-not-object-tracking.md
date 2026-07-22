# ADR 0001: Core Abstraction is Versioned State (`StateStream`), Not "Object Tracking"

## Status
Accepted

## Context
The originating brief framed the project around tracking C++ *objects*. During
research (see [../02-competitive-gap-analysis.md](../02-competitive-gap-analysis.md)
and [../03-core-idea-and-feasibility.md](../03-core-idea-and-feasibility.md)) it
became clear that objects, STL containers, ECS components, and allocator arenas are
all instances of the same underlying need: an ordered, queryable history of a value
over time. Designing the core around "object" specifically would force every
non-object producer (containers, ECS, arenas) to be shoehorned into an object-shaped
API later, or would require a second, parallel core for them.

## Decision
The core recording/storage layer operates on an opaque, type-erased primitive,
`StateStream`: an ordered sequence of `(version, value-or-delta, metadata)`. Objects,
fields, containers, and custom domain state are all *adapters* that produce
`StateStream`s; the core has no knowledge of what an "object" is at all (see
[../05-architecture.md](../05-architecture.md)).

## Consequences
- Positive: new kinds of trackable things (a physics grid, an order book) require
  only a new adapter, never a core change — directly supports the layered
  architecture and dependency rules in Phase 5.
- Positive: the Replay/Query/Visualization layers (Phases 5, 7, 8) are written once
  against `StateStream` and work identically regardless of what produced it.
- Negative: adds one layer of indirection (adapter → StateStream) that a
  purely object-centric design wouldn't need — judged acceptable given the payoff
  above.
- Follow-on: the public API's primary verb becomes `chronicle::track(anything
  satisfying Trackable, ...)` rather than an object-specific entry point (Phase 7).
