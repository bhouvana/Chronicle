# ADR 0021: PMR Allocator Adapter Lives in `include/chronicle/`, Not `adapters/allocator/`

## Status
Accepted

## Context
[10-roadmap.md](../10-roadmap.md)'s v2.0 scope calls for a "PMR allocator/
arena adapter." [04-technical-limitations.md](../04-technical-limitations.md)
already answered *whether* this is possible, in the original research
phase: "Can every allocation be discovered? Yes, if the allocator is ours
or hooked (operator new/delete, PMR, custom)." `std::pmr::memory_resource`
is exactly that hook, and — checked directly, not assumed — it is standard
library (`<memory_resource>`, C++17), not a third-party dependency.

[11-repository-structure-and-standards.md](../11-repository-structure-and-standards.md)'s
original layout sketch, and [ADR 0006](0006-container-tracking-lives-in-core-not-adapters.md)
(written for `tracked_vector<T>`), both list an allocator adapter under
`adapters/allocator/`, reserved for "adapters with a real external
dependency." Re-checked against that stated rule rather than assumed to
still apply: `<memory_resource>` has no external dependency at all — the
exact same situation ADR 0006 already found once for `tracked_vector<T>`
("has no such dependency... put it in `include/chronicle/` instead"). This
ADR applies that same precedent here and corrects the same category of
placement mistake docs/11's original sketch made once already, rather than
repeating it.

## Decision
`include/chronicle/tracked_memory_resource.hpp` (part of chronicle-core,
included from the umbrella `chronicle.hpp`, no opt-in build flag needed —
unlike every other adapter this project has built, since there is no
external dependency to opt out of): `TrackedMemoryResource` wraps an
upstream `std::pmr::memory_resource*` and records every `do_allocate`/
`do_deallocate` as a `MapOp<std::uint64_t, std::uint64_t>` event — `Insert`
keyed by the allocated address, valued by size in bytes; `Erase` keyed by
the freed address. This reuses `map_op.hpp`'s exact `KeyedOp` wire shape
`tracked_map<K,V>` already produces — the same design choice
[ADR 0015](0015-entt-adapter.md) made for the EnTT adapter, and for the
same reason: `chronicle-cli` and the HTML/live viewers need **zero
changes** to display allocation history, since they already know how to
render a `MapOp` stream.

`do_allocate` calls upstream *first* (the address is the event's key, so
there's nothing to record until upstream hands one back — a failed
`allocate()` throws before anything is recorded, same as any other
`memory_resource`). `do_deallocate` records the `Erase` *before* physically
freeing — symmetric with `ComponentTracker::on_destroy`'s ordering
(ADR 0015): once `upstream_->deallocate()` returns, the address may already
be handed back out by a subsequent `allocate()`, so the log entry for "this
address was freed" must unambiguously belong to this deallocation, not a
possible future reuse. `do_is_equal` compares identity only (`this ==
&other`) — standard practice for a resource with observable side effects,
since two different `TrackedMemoryResource` wrappers around the same
upstream track independent address sets, and treating them as
interchangeable would let a deallocation route through a stream that never
recorded the matching insert.

Non-copyable, non-movable — the same lifetime discipline already
established for `chronicle::tracy_bridge::PlotHandle` (ADR 0013) and
`chronicle::adapters::entt::ComponentTracker` (ADR 0015): a
`memory_resource`'s identity is tied to `this` (containers hold a raw
pointer to it), so relocating one after PMR containers are already using
it would be unsound regardless of anything this wrapper adds.

### Verification performed
`tests/unit/tracked_memory_resource_test.cpp` (4 tests) uses **real**
`std::pmr::vector<int>` — not a mock allocator — confirming: a single
allocation records the correct address and exact byte size: a
deallocation records `Erase` for the same address a prior `Insert` used;
two independent containers get independently-keyed events; and — the most
interesting case, not just the simplest one — **growing a vector past its
capacity** (`reserve()` forcing a real reallocation) correctly records an
`Erase` of the old address alongside the `Insert` of the new one.

`examples/allocator/main.cpp` is a full, real, end-to-end demonstration:
two `std::pmr::vector<int>`s, one deliberately grown past capacity to
force a reallocation, written to `demo_allocator.chronicle` by binding
`TrackedMemoryResource::stream()` to a `tracked_map<std::uint64_t,
std::uint64_t>` shell (reusing `session_writer.hpp`'s existing
serialization path, no new writer code needed). `chronicle-cli history`
against the real output showed all 6 events with real heap addresses,
byte-accurate sizes (`16`/`32`/`1024`, matching `sizeof(int)` × requested
capacity exactly), real call sites (`tracked_memory_resource.hpp:82`/`:94`
— `do_allocate`/`do_deallocate`), and the reallocation correctly captured
as `insert[new 1024-byte address]` immediately followed by
`erase[old 16-byte address]` — exactly the order `std::vector`'s own
grow-and-move-then-free implementation produces. Full suite: **257/257
checks across 53 tests**.

## Consequences
- Positive: chronicle-core gains real allocator-level tracking with zero
  new dependency and zero opt-in build flag — genuinely simpler to adopt
  than every other v1.0/v2.0 adapter, precisely because there was no
  external library to make optional in the first place.
- Positive: `chronicle-cli`/the viewers needed no changes at all — the
  second time reusing `MapOp`/`KeyedOp` has paid off this way (first for
  EnTT, ADR 0015), reinforcing that this wire shape generalizes well to
  "any Insert/Update/Erase-shaped event stream," not just container
  mutations specifically.
- Positive: corrects [11-repository-structure-and-standards.md](../11-repository-structure-and-standards.md)'s
  layout to reflect a distinction ADR 0006 already established for
  containers but hadn't yet been re-applied to the allocator adapter
  specifically until this ADR.
- Negative: only allocations that flow through a `std::pmr::memory_resource`
  Chronicle was told to wrap are visible — global `operator new`/`malloc`,
  or a container never constructed with this resource, remain exactly the
  documented blind spot [04-technical-limitations.md](../04-technical-limitations.md)
  already named ("no for allocators we never see"), not a regression this
  ADR introduces.
- Negative: address reuse across a program's lifetime means two unrelated
  allocations can share a `MapOp` key if the log is read without regard
  for ordering — the existing `Insert`/`Erase` sequence within the stream
  disambiguates this correctly (an `Insert` always precedes the `Erase` it
  pairs with), but a query that looks at only the latest event for a given
  address without replaying the whole sequence could misattribute it. Not
  a new hazard specific to this adapter — the same address-reuse caveat
  [ADR 0009](0009-lock-free-ring-buffer.md) and
  [ADR 0011](0011-tracked-type-explicit-handle.md) already document for
  address-keyed data in general.
