# API Design

## Design principles

- Zero-cost when untracked: a type with no `chronicle` annotations pays nothing, not
  even a vtable pointer — this is non-negotiable for C++ adoption (Herb Sutter's bar:
  "you don't pay for what you don't use").
- Tracking is **opt-in per-field**, never a blanket "watch this whole object" default
  — matches the retention/cost model in Phase 6 and avoids the "instrument
  everything, drown in noise" failure mode observed in verbose logging frameworks.
- The query surface (`history`, `diff`, `replay`) should read like the recording
  surface (`track`, `session`) — same vocabulary, same casing, symmetric verbs.
- Prefer free functions and small value types (`Timeline`, `Snapshot`, `Diff`) over
  deep class hierarchies — consistent with Phase 5's rejection of a monolithic God
  object.

## Core surface (sketch — not final signatures, establishes shape)

```cpp
#include <chronicle/chronicle.hpp>

struct Player {
    chronicle::tracked<int>          health   {100};
    chronicle::tracked<glm::vec3>    position {};
    chronicle::tracked_vector<Item>  inventory{};
};

// A session scopes recording lifetime & retention policy.
auto session = chronicle::session(chronicle::retention::ring_window(5s));

// Registration ties an instance to the active session (RAII-scoped).
chronicle::track(player, session, "player_1");

// --- elsewhere in the program, health mutates through the wrapper ---
player.health = player.health - 25;   // recorded automatically

// --- query API ---
auto hx = chronicle::history(player.health);        // Timeline<int>
for (auto const& rec : hx) {
    // rec.value, rec.timestamp, rec.thread_id, rec.call_site
}

auto snap_a = chronicle::snapshot(player, t0);
auto snap_b = chronicle::snapshot(player, t1);
auto diff   = chronicle::diff(snap_a, snap_b);       // Diff<Player>, field-level

// Exact alternative to time_point-based snapshot(): per-stream version order
// is the one ordering the system actually guarantees (ADR 0003), confirmed
// necessary in practice once fast back-to-back mutations could tie on the
// same clock reading under optimization (see ADR 0007). Prefer this whenever
// the caller controls versions directly rather than correlating with real
// wall-clock time.
auto v0  = chronicle::current_version(player.health);
auto snap_exact = chronicle::snapshot_at_version(player.health, v0);

chronicle::replay(session)
    .from(t0).to(t1)
    .on_change([](auto const& stream, auto const& event) { /* scrub callback */ });

chronicle::checkpoint(session, "level_loaded");      // explicit named snapshot
```

## Registration without wrapper types (macro / codegen path)

For types that must retain their original layout (ABI-sensitive structs, POD passed
to C APIs), an explicit registration macro avoids the `tracked<T>` wrapper entirely,
generating the interception hooks as free functions instead of member wrappers:

```cpp
struct NetworkPacket { int seq; float latency; };  // must stay POD, byte-for-byte

CHRONICLE_TRACK_TYPE(NetworkPacket, seq, latency);

NetworkPacket p{};
chronicle::track(p, session, "packet");
chronicle::set(p.seq, 42);   // explicit setter call records the mutation;
                              // p.seq = 42 directly does NOT (documented tradeoff)
```

This is the fallback the Instrumentation layer (Phase 5) falls back to whenever
`tracked<T>` isn't viable per the limitations enumerated in Phase 4 (bitfields,
ABI-locked structs, unions). It trades "assignment syntax is automatically tracked"
for "layout and existing code are untouched," and both paths coexist rather than one
replacing the other.

## Adapter surface (the `Trackable` concept family)

```cpp
template <typename T>
concept Trackable = requires(T& t, chronicle::Session& s) {
    { chronicle::adapt(t, s) } -> std::same_as<chronicle::StreamHandle>;
};
```

Built-in adapters implement `adapt()` for: scalar `tracked<T>`, `tracked_vector<T>` /
`tracked_map<K,V>`, and (as separate, optional libraries per Phase 5's dependency
rules) EnTT components and PMR allocator arenas. A user can write their own adapter
for a custom simulation grid without touching Chronicle's core — this is the
concrete payoff of the "versioned state, not objects" reframing from Phase 3: the
public API doesn't special-case what kind of thing is being tracked.

## Session, Timeline, Snapshot, Diff — the four core value types

- `Session`: owns a group of `StateStream`s and their shared retention/storage
  policy. RAII-scoped; ending a session finalizes (flushes or discards, by policy)
  everything recorded under it. Sessions can be nested logically via naming/tags but
  are never implicitly nested at the type level — keeps the mental model flat.
- `Timeline<T>`: an ordered, lazily-materialized view over one stream's events —
  iterable, filterable (`.since(t)`, `.where(pred)`), and directly usable with range
  adaptors (`std::views`) since C++20 ranges are the natural vocabulary for "ordered
  sequence of historical values."
- `Snapshot<T>`: a fully-materialized value of `T` (or an aggregate of tracked
  fields) as of a specific version/time — the thing you'd actually serialize/compare.
- `Diff<T>`: field-by-field (or element-by-element, for containers) comparison of two
  `Snapshot<T>`s, itself iterable (`for (auto const& change : diff)`), the primitive
  the visualization layer (Phase 8) renders directly.

## What we are explicitly NOT putting in the core API

- No implicit global registry ("track every object of type X automatically") —
  violates opt-in-per-field and zero-cost-when-untracked.
- No exceptions as the error-reporting mechanism on the hot `tracked<T>::operator=`
  path — failure there (e.g. ring buffer full under a `Block` policy) is reported via
  a configurable callback/counter, not a throw, to keep the hot path
  exception-machinery-free and `noexcept`-friendly for use in `noexcept` contexts
  (destructors, signal-adjacent code, game engine hot loops).
- No synchronous I/O anywhere reachable from `operator=` — matches Phase 5's
  hot-path/cold-path separation exactly.

## API review bar

Every public symbol must survive being asked, in the style of a Boost/LLVM/WG21
review: *does this compose with `std::ranges`? does this work in a `constexpr`
context where possible? does this have a sane default that requires zero
configuration to get first value? does the name read correctly at the call site
without the declaration in view* (`chronicle::track(player, session, "player_1")`
passes this; a hypothetical `chronicle::t(p, s, "p1")` would not).
