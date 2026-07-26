# ADR 0039: Persist Provenance and Derivation (Format v4 → v5)

## Status
Accepted

## Context
[docs/13-vision.md](../13-vision.md) named this the single highest-leverage
remaining item after the vision cycle's first pass: both
[ADR 0032](0032-provenance-stacktrace.md)'s `set_with_stacktrace()`/
`provenance_of()` and [ADR 0033](0033-derived-state.md)'s `derive()`/
`explain()` were explicitly **in-process only** — their
`(stream_id, version)`-keyed registries genuinely don't exist once the
producing process exits, which is why `chronicle-cli narrate`
([ADR 0038](0038-narrative-composer.md)) could only show a single
persisted call site, never a full chain, and nothing at all of a
derivation's explanation.

## Decision
**One shared wire-format decision**, not two separate ones, per the vision
doc's own framing.

`include/chronicle/io/format.hpp` bumps `kFormatVersion` 4 → 5 (a clean,
non-backward-compatible break — no migration, same as every prior bump)
and adds `kExtendedSectionsMarker`, a stream-name-length value no real
stream could ever have (`0xFFFFFFFFFFFFFFFF`), written once, always, right
after the last stream block. This is necessary because the format has no
leading stream count — the block loop always ran to genuine end-of-file —
and v5 needs to write real data *after* the streams, so true EOF can no
longer be the loop's stop signal. Same "an impossible value is an
unambiguous sentinel" convention this format already uses for
`HlcTimestamp{0,0}` and `call_site.line == 0`.

Two new entry types, reusing the exact structs ADR 0032/0033 already
defined rather than duplicating their shape: `ProvenanceEntry{stream_name, version, frames}`
wraps `chronicle::provenance::StackFrame`; `DerivationEntry{stream_name, version, changes}`
wraps `chronicle::derived::DependencyChange`. Both key on the stream's
**name**, not its in-process-only `id()` — name is the one identifier
that survives a save/load round trip at all.

`SessionWriter` gains `write_provenance(tracked<T> const&)`/
`write_derived(tracked<T> const&)` — explicit and opt-in, exactly like
`write<T>()` itself, since most fields never call `set_with_stacktrace()`/
`derive()` at all and there'd be nothing to persist for them. Both buffer
into new member vectors rather than writing immediately, so ordering is
correct regardless of what sequence the caller calls
`write()`/`write_provenance()`/`write_derived()` in; the destructor writes
the sentinel and both tables (always, even empty) right before the
existing compress-and-flush logic — the same method, extended, not a
second code path.

`LoadedSession` gains `provenance`/`derivations` vectors and
`provenance_for()`/`derivation_for()` lookups by `(stream_name, version)`.
`chronicle-cli narrate` was updated in the same cycle to actually use
them: a field with a persisted call chain shows the full chain; a field
with a persisted derivation explanation shows it; everything else falls
back to the single call site exactly as before.

### Verification performed
`tests/unit/io_test.cpp` gained 3 tests: a file written with no
`write_provenance()`/`write_derived()` calls at all (the common case)
round-trips with correctly empty `provenance`/`derivations`; a field using
`set_with_stacktrace()` round-trips a real, non-empty call chain, correct
per-version (the version that never captured a trace correctly has none
persisted for it); a `derive()` target round-trips its real
`DependencyChange` list with correct attribution. Full suite:
**421/421 checks across 105 tests.**

CLI: a real generated file with one `set_with_stacktrace()` field, one
`derive()` binding, and two plain fields, run through `chronicle-cli narrate`
— the stacktrace field showed its actual 5-frame call chain with real
resolved `file:line` for each user-code frame; the derived field correctly
attributed its change to the dependency that actually moved; both plain
fields correctly fell back to the existing single-call-site display.

## Consequences
- Positive: Layers 3, 4, and 10 are now meaningfully more complete —
  provenance and derivation data survive a process exit, and `narrate`
  delivers the fuller version ADR 0038 said would "fall out for free once
  persistence exists."
- Positive: one mechanism (sentinel + two tables) serves both features,
  not two independent format changes — directly the outcome the vision
  doc asked for.
- Positive: the common case (no provenance/derivation at all) is unchanged
  in cost beyond a few fixed bytes (the sentinel + two zero counts) per
  file — no per-event cost for fields that never opted in.
- Negative: every v5 file is now slightly larger than a v4-equivalent one
  even with nothing persisted (a handful of extra bytes) — a real,
  accepted, one-time fixed cost, not a per-event one.
- Negative: `write_provenance()`/`write_derived()` re-scan a field's full
  history on every call (`O(history size)`), same complexity class as
  `write<T>()` itself already has — acceptable for this project's
  established "cold I/O path, not the hot record() one" cost model.
