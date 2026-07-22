# ADR 0015: EnTT Adapter Tracks Fields via `TrackedFieldsOf<T>`, Reusing the `MapOp` Wire Shape

## Status
Accepted

## Context
[10-roadmap.md](../10-roadmap.md)'s v1.0 scope calls for `chronicle-adapter-entt`:
bridge an `entt::registry`'s component lifecycle into Chronicle history, so
an ECS-based program gets tracked fields without hand-writing a `track()`
call at every mutation site. Two real constraints shaped the design, both
found by reading EnTT's actual headers rather than assumed:

- **A component's whole value usually isn't serializable.** Chronicle's
  wire format (`include/chronicle/io/wire.hpp`) is a closed vocabulary —
  `WireCodec<T>` only exists for arithmetic types and `std::string`. Real
  ECS components are almost always small aggregates (`struct Position {
  float x, y; }`), which have no `WireCodec` specialization and never will
  (no generic mechanism serializes an arbitrary struct — that's the exact
  problem [ADR 0011](0011-tracked-type-explicit-handle.md)'s
  `CHRONICLE_TRACK_TYPE` already solved once, field-by-field, for
  `tracked<T>`-adjacent code). Tracking a component's *whole value* as one
  opaque blob was rejected first for this reason — it would only work for
  single-field components, silently failing (a compile error, at best) for
  every realistic one.
- **EnTT's signal API is a compile-time-function-pointer-plus-runtime-
  payload delegate**, not a `std::function`: `registry.on_construct<T>()`
  returns a `sink`, and `sink::connect<&Candidate>(instance)` binds a
  function/member pointer known at compile time to a runtime payload
  reference — verified directly in `entt/signal/delegate.hpp`, not assumed
  from prior EnTT familiarity. This is structurally the same shape this
  project already chose for `Stream<T>::RecordHook`
  ([ADR 0013](0013-tracy-bridge.md)), which made the connection natural
  once seen.

## Decision
`track_component<Component>(registry, session, name)`
(`adapters/entt/include/chronicle/adapters/entt.hpp`) requires `Component`
to already have a `CHRONICLE_TRACK_TYPE(Component, field1, ...)`
registration — reusing `TrackedFieldsOf<Component>`
(`include/chronicle/tracked_type.hpp`) rather than inventing a second
reflection mechanism. A component with no such registration is a compile
error at the `track_component<Component>()` call site (`TrackedFieldsOf`'s
deliberately-undefined primary template, same signal ADR 0011 already
relies on elsewhere), not a runtime surprise.

For each of `Component`'s registered fields, `ComponentTracker<Component,
FieldTypes...>` creates one `Stream<MapOp<std::uint32_t, FieldType>>`
(`chronicle/map_op.hpp`), keyed by `entt::to_integral(entity)` — reusing
`tracked_map<K,V>`'s exact `KeyedOp`/`MapOp` wire shape rather than adding a
new one. This is the load-bearing design choice: `chronicle-cli` and the
HTML viewer already know how to display a `MapOp` stream (they were built
for `tracked_map<K,V>` and have no idea this data came from EnTT), so this
adapter needed **zero changes** to either — verified by not touching them
at all and having the existing infrastructure just work. `entt`'s own
construct/update/destroy vocabulary maps directly onto `ContainerOpKind`
(`Insert`/`Update`/`Erase`) with no translation layer.

The three EnTT signals connect straightforwardly: `on_construct` and
`on_update` both call `registry.get<Component>(entity)` and record every
field as `Insert`/`Update` respectively; `on_destroy` — verified to fire
**before** removal (`entt/entity/registry.hpp`'s documented guarantee, so
`get<Component>()` is still valid inside the handler) — records `Erase`
with each field defaulted to `FieldType{}`, matching
`tracked_map<K,V>::erase()`'s own established "Erase carries no value"
convention exactly. Any entity that already has `Component` when
`track_component()` is called is backfilled as an `Insert` at construction
time, matching `track()`/`track_type()`'s "history should never start
empty" convention elsewhere in this codebase.

`ComponentTracker` is non-copyable and non-movable: EnTT's `sink::connect`
binds a raw reference to `*this`, so relocating the tracker would leave the
registry's signal table pointing at a stale address — the same
`tracy_bridge::PlotHandle` lifetime discipline
([ADR 0013](0013-tracy-bridge.md)), for the same underlying reason (an
external system holds a raw pointer/reference into this object).

`adapters/entt/` is a genuinely separate CMake target
(`chronicle-adapter-entt`), per
[11-repository-structure-and-standards.md](../11-repository-structure-and-standards.md)'s
reserved `adapters/` directory for "adapters with a real external
dependency" — unlike `tracked_vector<T>`/`tracked_map<K,V>`
([ADR 0006](0006-container-tracking-lives-in-core-not-adapters.md)),
this one has one (EnTT), so it does *not* live in `include/chronicle/`.
Opt-in via `CHRONICLE_BUILD_ENTT_ADAPTER` (default `OFF`) and
`find_package(EnTT CONFIG QUIET)` with a graceful skip when not found — the
same pattern `tools/codegen`, the Tracy bridge, and Zstd compression
already established.

### Verification performed
`tests/unit/entt_adapter_test.cpp` (opt-in, only built when EnTT's CMake
package is found — same reasoning as `compression_test.cpp`) — six tests:
insert-on-construct, update-on-patch, erase-on-remove with no carried value,
independent per-field streams, backfilling entities that predate the
tracker, and independent keys across multiple entities. Default build
(`CHRONICLE_BUILD_TESTS` alone) stays at **208/208 checks, 38 tests**,
completely unchanged; with EnTT (and Zstd) available, **246/246 checks
across 50 tests**.

`examples/entt-integration/main.cpp` exercises a *real* `entt::registry` —
`emplace`/`patch`/`remove`, not a mock — and prints the resulting
`position.x` stream history, confirming byte-for-byte the expected
sequence: `Insert(entity=0, x=1.0)`, `Insert(entity=1, x=10.0)`,
`Update(entity=0, x=5.0)`, `Erase(entity=1, x=0.0)` — the exact order and
values the four EnTT calls in that file should produce, not just "it
compiled."

## Consequences
- Positive: `chronicle-cli`/the HTML viewer gained EnTT-sourced history
  support with zero code changes to either — a direct payoff of reusing
  `MapOp`/`KeyedOp` instead of inventing an EnTT-specific wire shape.
- Positive: chronicle-core gains no EnTT dependency whatsoever;
  `adapters/entt/` is the only place that includes `<entt/entt.hpp>`, and
  the default build (`CHRONICLE_BUILD_ENTT_ADAPTER=OFF`) is unaffected —
  verified, not assumed, via the unchanged 208/208 default-build result.
- Positive: `TrackedFieldsOf<T>` (built for `CHRONICLE_TRACK_TYPE`,
  [ADR 0011](0011-tracked-type-explicit-handle.md)) turned out to be
  exactly the reflection primitive this adapter needed too — a real payoff
  of that earlier design choice, not a coincidence engineered in advance.
- Negative: only components with an existing `CHRONICLE_TRACK_TYPE`
  registration can be tracked — a component the user hasn't (or can't,
  e.g. a third-party type) annotate has no path into this adapter yet. Not
  solved speculatively; a real follow-up if it becomes a practical
  blocker.
- Negative: `EntityKey` is `entt::to_integral(entity)` — the raw
  integral value alone, not entity+version together. Two different
  entities that reuse the same integral slot after a destroy/recreate cycle
  (EnTT's own versioning exists specifically to distinguish this case)
  would appear as the same key in a replayed `MapOp` history. Not
  addressed in this pass: `WireCodec` only has arithmetic/string leaves, so
  encoding a versioned identity as the map key would need either a second
  key type or a composite encoding, either a real but separate follow-up.
