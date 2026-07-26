# ADR 0030: C++26 Reflection (P2996) Feature Gate — Environmental Wall, Not an Implementation

## Status
Accepted (as a documented wall, not a shipped registration backend)

## Context
[docs/12-future-research-topics.md](../12-future-research-topics.md) topic 2
asks whether C++26 static reflection (P2996) could supersede
`CHRONICLE_TRACK_TYPE`'s manual macro
([ADR 0011](0011-tracked-type-explicit-handle.md)) and the Clang-LibTooling
codegen tool ([ADR 0012](0012-chronicle-codegen-libtooling.md)) with
genuinely automatic field discovery, and whether that's possible without
an API break.

## Decision
Rather than write a P2996-based implementation this project has no way to
compile or test — which would be exactly the "should work" claim this
project's standing ethos rejects (the same bar
[04-technical-limitations.md](../04-technical-limitations.md) and every
prior ADR holds itself to) — this ADR documents a **real, verified**
environmental wall, the same honesty pattern as
[ADR 0022](0022-vscode-extension.md)'s VS Code Electron GUI gap.

**Verified directly, not assumed**: a tiny probe program
(`#if defined(__cpp_reflection)`) was compiled and run against **both**
toolchains actually available in this build environment — MSVC 19.44
(VS2022 17.0 Build Tools) and Clang 21.1.6 (the exact compiler
`bench/baseline.json`'s own numbers were captured with, confirmed present
on `PATH`). **Neither defines `__cpp_reflection`.** No compiler this
project's authors could access supports P2996 today.

`include/chronicle/reflect_p2996.hpp`: a single, real, always-compiles
constant, `chronicle::reflect::p2996_available`, gating an empty
`#if defined(__cpp_reflection)` block where a real implementation belongs
once it can be written and tested. Not included from the umbrella header —
nothing here changes chronicle-core's behavior on any compiler this
project can currently build with. `tests/unit/reflect_p2996_test.cpp` is a
deliberate **canary**: it asserts today's real, verified state and is
*meant* to fail the day a CI compiler starts defining `__cpp_reflection` —
a build break that correctly signals "implement and verify the real
backend now," rather than this gap staying silently stale.

**What the real implementation would still need to resolve, left for
whoever writes it against a real compiler** (recorded here so that work
doesn't start from zero): whether to keep `CHRONICLE_TRACK_TYPE`'s 8-field
cap (ADR 0011's "documented, mechanically extensible limit, not a
fundamental one") now that reflection makes arbitrary field counts easy;
whether it replaces or coexists with the macro/codegen paths, given
04-technical-limitations.md's note that macros don't propagate across
C++20 module boundaries — a real, separate reason a reflection-based path
might matter beyond just "less typing"; and how to produce the exact same
`TrackedType<T, FieldTypes...>` handle shape the existing paths produce, so
downstream consumers need zero changes regardless of registration backend.

## Consequences
- Positive: no unverifiable, potentially-wrong P2996 syntax added to the
  codebase — a real risk this ADR explicitly declined to take.
- Positive: the wall is verified with evidence (two real compilers probed
  and confirmed), not assumed from release notes or general knowledge —
  consistent with this project's "verify for real" standard.
- Positive: a real canary test exists that will correctly force this topic
  back onto the agenda the moment it becomes actionable, rather than
  relying on someone remembering to re-check.
- Negative: topic 2 is not "shipped" in the sense every other docs/12 topic
  in this cycle was — there is no new capability a caller can use today.
  This ADR documents why that's the honest outcome, not a shortfall in
  effort.
