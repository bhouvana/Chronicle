# ADR 0010: Call-Site Capture — `operator=` Cannot Do It, Named Methods Can

## Status
Accepted

## Context
[10-roadmap.md](../10-roadmap.md)'s v0.5 scope includes "causal-chain queries
('what last wrote this value')." [07-api-design.md](../07-api-design.md)'s
original API sketch already assumed this was solved — `rec.call_site` appears
in the very first example in that document — but no field for it existed
anywhere in the implementation until this pass.

The obvious design was: give `tracked<T>::operator=` a second, defaulted
`std::source_location` parameter (`std::source_location::current()`), the
same pattern used throughout the codebase for `track()`, `push_back()`, and
every other entry point that needs "where was I called from." This does not
compile. Confirmed directly, not assumed from memory of standardese:

```cpp
Wrapper& operator=(T v, std::source_location loc = std::source_location::current()) {
    ...
}
// error: parameter of overloaded 'operator=' cannot have a default argument
```

A non-static member `operator=` is required by the language to have exactly
one parameter — full stop, no exception for defaulted ones. This is not a
style lint; it is a hard compile error on every compiler. Consequently,
**plain `field = value` assignment can never capture its own call site**,
in this or any future version of `tracked<T>`, without changing what
`operator=` fundamentally is (e.g. dropping assignment syntax entirely).
This is a permanent property of the design, not a gap to close later.

## Decision
Two tiers, matching what the language actually allows:

- **Named methods capture correctly.** `Stream<T>::record()`,
  `chronicle::track()`, `chronicle::set()`, `tracked_vector<T>::push_back()/
  update()/erase()/clear()`, and `tracked_map<K,V>::set()/erase()/clear()`
  are all plain functions, not operators, so each takes its own
  `std::source_location call_site = std::source_location::current()` and
  captures its *direct* caller correctly.
- **`tracked<T>::operator=` explicitly records "unknown."** Rather than let
  `Stream<T>::record()`'s own default kick in (which would silently report
  the fixed line inside `operator=`'s body, inside `tracked.hpp`, for *every*
  plain assignment across the entire program — actively misleading, not
  merely imprecise), `operator=` passes an explicit, empty
  `std::source_location{}`. `event.hpp`'s `call_site.line() == 0` is the
  documented, checkable signal for "not captured here." `chronicle::set(field,
  value)` is the alternative for callers who want the call site and are
  willing to spell it explicitly instead of using `=`.

A wrapper that forwards to a lower-level function *must* explicitly pass its
own captured `call_site` rather than omit the argument — omitting it does
not mean "no call site," it means "silently re-capture whatever line inside
this library happens to call the next layer down." `push_back()` and
`chronicle::set()` both do this correctly; it is called out explicitly in
each file's comments as the one easy way to get this quietly wrong.

## Consequences
- Positive: `HistoryRecord<T>::call_site` is honest — a `known() == true`
  entry is always attributable to real user code, never a library internal.
- Positive: the on-disk format (`.chronicle`, format v2 — see
  `include/chronicle/io/format.hpp`) carries this through unchanged; the CLI
  and HTML export both render `[filename:line]` only when known, and render
  nothing (not a fabricated placeholder) when not — consistent with this
  project's established rule (docs/04, docs/08) of never presenting a gap
  as if it were data.
- Negative: the most ergonomic, most commonly used spelling (`field =
  value`) is also the one that can never carry a call site. Users who want
  attribution must know to reach for `chronicle::set()` instead — a real,
  permanent asymmetry in the API surface, not a temporary rough edge.
- Verified: `tests/unit/call_site_test.cpp` checks both sides of the
  asymmetry directly (plain assignment → `!known()`, `chronicle::set()` →
  `known()` with the correct file/line), plus round-tripping through the
  on-disk format (`tests/unit/io_test.cpp`).
