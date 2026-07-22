# ADR 0006: `tracked_vector<T>` Lives in `include/chronicle/`, Not `adapters/stl/`

## Status
Accepted

## Context
[11-repository-structure-and-standards.md](../11-repository-structure-and-standards.md)'s
proposed layout put `tracked_vector`/`tracked_map` under `adapters/stl/`,
grouped with `adapters/entt/` and `adapters/allocator/` as separately-buildable
adapter modules — reasonable when that layout was written, since
[05-architecture.md](../05-architecture.md) frames all container/domain
tracking as "adapters" on top of a type-agnostic core.

Implementing `tracked_vector<T>` (v0.2, docs/10-roadmap.md) surfaced a
distinction the original layout didn't make: `adapters/entt/` and
`adapters/allocator/` exist as separate CMake targets specifically so a
project that doesn't use EnTT (or a custom allocator) never links against it
— a real, external, optional dependency each one pulls in.
`tracked_vector<T>` has no such dependency: it only needs `<vector>`, exactly
like `tracked<T>` itself (already in `include/chronicle/`, not a separate
module). Putting it under `adapters/stl/` as a separately-toggled target would
add build-system indirection with no corresponding payoff — there's no
dependency to opt out of.

## Decision
`ContainerOp<T>`, `tracked_vector<T>`, and their `history()`/`snapshot()`/
`diff()` overloads live in `include/chronicle/` alongside `tracked<T>`, and
are part of the same `chronicle-core` target (`src/CMakeLists.txt`) rather
than a separate `adapters/stl` target. The `adapters/` directory
(`docs/11`'s layout) is reserved for adapters that genuinely pull in an
external dependency a project might reasonably want to exclude — `entt`,
`allocator`, and similarly-shaped future adapters. `tracked_map<K,V>` (shipped
shortly after this ADR) followed the same rule and also lives in
`include/chronicle/`, not
`adapters/stl/`.

## Consequences
- Positive: no build-system indirection for something with nothing to opt out
  of; `chronicle-core` remains the one target most consumers link against for
  both scalar and STL-container tracking.
- Positive: corrects [11-repository-structure-and-standards.md](../11-repository-structure-and-standards.md)'s
  layout to reflect a distinction (dependency-bearing vs. dependency-free
  adapters) that wasn't visible until a concrete adapter was actually built —
  consistent with that document's own "living document" commitment, and the
  same kind of build-time discovery [ADR 0004](0004-mutex-staging-deque-for-v01.md)
  and [ADR 0005](0005-cli-requires-on-disk-format.md) already recorded.
- Negative: `docs/11`'s originally-sketched tree diagram is now stale in this
  one respect until edited — tracked as a documentation follow-up alongside
  this ADR, not left silently inconsistent.
