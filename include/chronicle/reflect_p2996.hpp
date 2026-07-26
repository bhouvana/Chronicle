#pragma once

// docs/12-future-research-topics.md topic 2, docs/adr/0030-p2996-reflection-gate.md.
//
// Real, verified environmental wall (not assumed): as of this writing,
// neither MSVC 19.44 (VS2022 17.0) nor Clang 21.1.6 -- both toolchains
// actually available in this project's build environment, the latter the
// exact compiler bench/baseline.json's own numbers were captured with --
// define __cpp_reflection, confirmed by compiling and running a real
// probe program against both, not assumed from reading either compiler's
// release notes. No compiler this project's authors could access exists
// to write and test a real P2996-based registration backend against.
// Same honesty standard as docs/adr/0022's VS Code Electron gap: document
// the wall, don't fabricate an implementation no compiler here could
// confirm compiles or behaves correctly.
//
// This header is deliberately small: a single, real, always-compiles
// feature gate every future attempt at this topic should reach for rather
// than re-deriving the check, not a speculative implementation. Not
// included from chronicle.hpp -- there is nothing here yet that changes
// chronicle-core's behavior on any compiler this project can currently
// build with.

namespace chronicle::reflect {

// True only once a real toolchain defines __cpp_reflection. constexpr and
// evaluable today on every compiler in this project's CI matrix, unlike
// the actual P2996-based registration backend this topic asks for, which
// cannot exist as tested, verified code until this is true somewhere.
inline constexpr bool p2996_available =
#if defined(__cpp_reflection)
    true
#else
    false
#endif
    ;

#if defined(__cpp_reflection)
// A real P2996-based automatic registration backend belongs here, as an
// alternative to CHRONICLE_TRACK_TYPE's macro (docs/adr/0011-tracked-type-explicit-handle.md)
// and the Clang-LibTooling codegen tool (docs/adr/0012-chronicle-codegen-libtooling.md),
// producing the exact same TrackedType<T, FieldTypes...> handle shape
// those paths do so every downstream consumer (chronicle-cli, the
// viewers) needs zero changes regardless of which backend registered a
// given type. Deliberately NOT written speculatively here: no compiler
// this project's authors could access defines __cpp_reflection, so a
// hand-written implementation would be untestable, unverifiable P2996
// splicing syntax -- exactly the "should work" claim this project's
// standing ethos rejects (see memory.md's standing instructions / every
// ADR in this document set). Whoever first has access to a real
// P2996-capable compiler should implement and verify this from scratch
// against a real TrackedType<T, ...> comparison to the macro-registered
// path, not trust an unverifiable sketch nobody here could compile.
#endif

} // namespace chronicle::reflect
