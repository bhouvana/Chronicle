# ADR 0011: `CHRONICLE_TRACK_TYPE` Uses an Explicit Handle, Not an Address-Keyed Registry

## Status
Accepted

## Context
[07-api-design.md](../07-api-design.md)'s original sketch for ABI-sensitive/
POD types (structs that must stay byte-identical, so `tracked<T>`'s wrapper
approach isn't viable) showed:

```cpp
CHRONICLE_TRACK_TYPE(NetworkPacket, seq, latency);
NetworkPacket p{};
chronicle::track(p, session, "packet");
chronicle::set(p.seq, 42);   // explicit setter call records the mutation
```

Implementing this literally requires `chronicle::set(p.seq, 42)` — given
only a bare `int& seq` reference, with no other context — to recover which
`Stream<int>` that field belongs to. The only way to do that is a registry
keyed by the field's *address* (`&p.seq` → its `Stream<int>*`), populated
by `track()` and consulted by `set()`.

This is exactly the class of bug [ADR 0009](0009-lock-free-ring-buffer.md)
already found and fixed elsewhere in this codebase: stack addresses get
reused across distinct object lifetimes constantly (that ADR's own root
cause), so an address-keyed lookup with no way to invalidate stale entries
on destruction can silently resolve a *new* object's field to a *previous*
object's stream. `track()` for a POD struct has no destructor hook to
unregister on scope exit (that's the whole reason this path exists instead
of a wrapper type), so there is no clean way to keep such a registry
correct — the same hazard, with no available fix this time.

## Decision
`CHRONICLE_TRACK_TYPE(Type, field1, ..., fieldN)` (`include/chronicle/
tracked_type.hpp`) generates a `TrackedFieldsOf<Type>` specialization
holding compile-time metadata (member pointers + field name strings, up to
8 fields — a documented, mechanically extensible limit, not a fundamental
one). `chronicle::track_type(instance, session, name)` uses that metadata to
build one `Stream<FieldT>` per field and returns an explicit
`TrackedType<Type, FieldTypes...>` handle the caller holds onto:

```cpp
NetworkPacket p{};
auto tracked = chronicle::track_type(p, session, "packet");
tracked.set<0>(42);   // writes p.seq = 42 AND records it
tracked.get<0>();     // reads p.seq
```

Index-based (`set<0>`), not name-based (`set_seq`): generating a variable
number of *named methods* would need meaningfully more macro machinery than
generating a comma-separated argument list, for an ergonomics win only, not
a safety one. Not attempted speculatively — a real follow-up if index-based
access proves too unergonomic in practice.

## Consequences
- Positive: no address-keyed lookup exists anywhere in this feature, so the
  exact bug class ADR 0009 spent real debugging time on cannot recur here —
  the handle itself, not a global table, is what associates `p` with its
  streams, and the handle's lifetime is the caller's problem to manage the
  same way `tracked<T>`'s stream pointer already is.
- Positive: `NetworkPacket`'s layout is verified unchanged by a real test
  (`tests/unit/tracked_type_test.cpp`'s `track_type_preserves_pod_layout`,
  comparing `sizeof`/`alignof` against a plain equivalent struct) — the
  actual reason this path exists, not just asserted.
- Negative: deviates from docs/07's original sketch — `chronicle::set(p.seq,
  42)` reads slightly more naturally than `tracked.set<0>(42)`. Judged
  worth it: the sketch's syntax was never implemented, so nothing depends on
  it, and the alternative it implies is a real, previously-encountered
  safety hazard, not a hypothetical one.
- Follow-on: the Clang-based codegen tool (docs/10-roadmap.md's v0.5 item)
  targets this exact API — it exists specifically so hand-writing
  `CHRONICLE_TRACK_TYPE(Type, field1, ...)` and remembering to keep the
  field list in sync with the struct definition isn't required.
