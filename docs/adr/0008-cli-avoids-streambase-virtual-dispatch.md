# ADR 0008: Serialization Uses Plain Function Templates, Not a `StreamBase` Virtual Method

## Status
Accepted

## Context
Building the on-disk `.chronicle` format for v0.2
([10-roadmap.md](../10-roadmap.md), unblocking `chronicle-cli` per
[ADR 0005](0005-cli-requires-on-disk-format.md)), the first design considered
was a `write_wire()` virtual method on `StreamBase`
([05-architecture.md](../05-architecture.md)'s existing type-erasure
boundary), so `Session` could generically ask every stream it owns to
serialize itself without knowing each one's `T`.

This turns out to be unsafe in C++. `Stream<T>` is a class template; when a
virtual member function of a class template is not otherwise instantiated,
whether the compiler still implicitly instantiates it anyway (because a
vtable needs a concrete address for every virtual slot) is *unspecified*
by the standard ([temp.inst]). In practice this means: the moment any
program instantiates `Stream<SomeCustomStruct>` — e.g. by writing
`tracked<SomeCustomStruct> field; chronicle::track(field, session, "x");`,
with no intention of ever saving it — a compiler that eagerly instantiates
virtual members (Clang, the one this project builds and tests against,
does) would try to compile `write_wire()`'s body for `SomeCustomStruct`.
Since `WireCodec<SomeCustomStruct>` doesn't exist (wire.hpp's vocabulary is
deliberately small and closed — arithmetic types and `std::string` only),
this produces a hard compile error for code that never touches
serialization at all — breaking `tracked<T>`'s "works for any `T`"
guarantee that has held since v0.1. Other compilers might instead defer
instantiation and produce a *linker* error only if something actually calls
the method — different failure mode, same underlying breakage, and worse:
compiler-dependent, meaning the same code could compile on one toolchain
and fail on another.

## Decision
Serialization is a set of plain function templates
(`chronicle::io::SessionWriter::write<T>(tracked<T> const&)` and the
`tracked_vector`/`tracked_map` overloads, in
`include/chronicle/io/session_writer.hpp`), not a virtual method anywhere
on `StreamBase` or `Stream<T>`. `StreamBase`/`Stream<T>`
(`include/chronicle/session.hpp`, `stream.hpp`) are completely unmodified by
this feature. A plain function template is only ever instantiated when the
caller actually calls it for a specific `T` — ordinary, well-specified
template instantiation rules apply, no vtable involved, so a type with no
`WireCodec` simply never has `write<T>()` instantiated unless someone
explicitly tries to serialize it, at which point the compile error is
exactly where it belongs.

The tradeoff this accepts: there is no `Session::save_all()` that generically
walks every owned stream — the caller lists exactly which fields to persist
(`writer.write(player.health); writer.write(inventory); ...`). This is
judged a feature, not just an accepted cost: it makes persistence
opt-in per field, consistent with tracking itself being opt-in per field
(docs/07-api-design.md's core principle), and it means a program can choose
to persist a subset of what it tracks.

## Consequences
- Positive: `tracked<T>` for an arbitrary, non-serializable `T` continues to
  compile and work exactly as it did in v0.1 — serialization support adds
  zero risk to code that never uses it, on any compiler.
- Positive: the reader side (`chronicle::io::load_session`,
  `loaded_session.hpp`) has the same property in reverse — it needs no
  `Session`/`Stream<T>` machinery at all, since it operates purely on
  `WireValue`s (confirmed by `chronicle-cli` linking only
  `chronicle::core`'s io headers, no producer-side types).
- Negative: no single call persists "everything this session is tracking" —
  each field must be named explicitly at the save site. Consistent with the
  opt-in philosophy above, but worth knowing if a future use case genuinely
  wants blanket persistence (would need a different mechanism, likely a
  registry the tracked fields opt into separately from `Session` itself).
- Verified: `tests/unit/io_test.cpp` round-trips scalar, vector, and map
  streams through `SessionWriter` → `load_session`; `examples/export/` and
  `tools/cli/` were exercised end-to-end as genuinely separate processes
  (the CLI reading a file the export example wrote, sharing no C++ types).
