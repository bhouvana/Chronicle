# ADR 0012: `chronicle-codegen` Built Directly Against Clang LibTooling

## Status
Accepted

## Context
[docs/10-roadmap.md](../10-roadmap.md)'s v0.5 item calls for a Clang-based
codegen tool that scans for annotated structs and emits
`CHRONICLE_TRACK_TYPE(...)` registrations, closing the reflection gap
(Phase 4) without requiring C++26/P2996. Building it required two things
this codebase didn't have yet: a real marker attribute, and a Clang/LLVM
LibTooling toolchain — neither existed at project start, and both surfaced
their own real problems rather than working as first assumed.

### The roadmap's sketched marker doesn't survive to the AST
The roadmap text sketched `[[chronicle::track]]` as the annotation. Verified
directly with `-Xclang -ast-dump` rather than assumed: Clang emits a
"unknown attribute ignored" warning for unrecognized scoped attributes and
drops them with **no trace at all** in the AST — a `RecursiveASTVisitor`-
based tool has nothing to find. `[[clang::annotate("...")]]` is Clang's
actual supported mechanism for this exact use case and does survive into the
AST as an inspectable `AnnotateAttr` node (confirmed with the same dump).
`include/chronicle/tracked_type.hpp`'s `CHRONICLE_TRACKABLE` macro expands
to the `clang::annotate` form; it is a silent no-op under any other
compiler, same as any unrecognized vendor-scoped attribute, so annotated
code still compiles everywhere — only running the codegen tool itself
requires Clang.

### Building LLVM/Clang from source on this toolchain
No prebuilt Windows LLVM distribution ships the C++ development libraries
`clangTooling` etc. link against, so the only path was building LLVM/Clang
from source (`LLVM_ENABLE_PROJECTS=clang`, `clangTooling` and its
dependencies only — not the full `clang.exe` driver or LLVM's own tools,
to keep the build tractable). This surfaced a real, reproducible toolchain
bug, isolated methodically rather than worked around blindly:

**CMake + Ninja + `cl.exe` fails `TryCompile` with D8050** ("cannot execute
c1.dll") on this machine, for a trivial CMake project — not specific to
LLVM's own build. Ruled out, in order: transient failure (retried clean,
still failed), compiler flags/`/MP`, path length, antivirus interference
(added a Defender exclusion, still failed), and tool sandboxing (retried
with sandboxing disabled, still failed). Isolated to CMake's internal
`TryCompile` mechanism specifically under the Ninja generator — the same
trivial project configures and builds fine under the NMake Makefiles
generator. Fix: `-DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_CXX_COMPILER_WORKS=1`
skips the broken check while keeping Ninja's real (working, parallel)
build — validated with an actual compile+link+run smoke test before
committing to it for the full LLVM build.

## Decision
`tools/codegen/main.cpp` is a `RecursiveASTVisitor`-based `ClangTool` that
finds `CXXRecordDecl`s carrying the `chronicle::track` `AnnotateAttr`,
collects their direct data members (warning and skipping structs with zero
fields or more than `CHRONICLE_TRACK_TYPE`'s 8-field limit, rather than
emitting code that won't compile), and prints
`CHRONICLE_TRACK_TYPE(Type, field1, ...)` per struct.

Built and verified against a from-source LLVM/Clang tree
(`LLVM_ENABLE_PROJECTS=clang`, `LLVM_BUILD_TOOLS=OFF`,
`CLANG_BUILD_TOOLS=OFF` — only the libraries, not the tools) with a direct
compile+link line rather than `find_package(Clang)`, because that build
configuration does not generate `ClangConfig.cmake`/`LLVMConfig.cmake`
(those are produced by the `install` target/a tools build, neither of which
was done here). Getting that direct link to succeed required diagnosing,
in order:
1. **CRT mismatch (`LNK2038`)**: the LLVM libraries were built under
   CMake's default MSVC Release flags, which link the dynamic CRT (`/MD`).
   Clang's plain `clang++` driver (not `clang-cl`) does not default to
   either CRT model for an MSVC target — it must be told explicitly via
   `-fms-runtime-lib=dll` to match, or every LLVM object's embedded
   `RuntimeLibrary` linker metadata conflicts with the compiled
   `main.cpp`'s.
2. **Missing default libs (`LNK2019`, `__imp_*` symbols)**: once the CRT
   model matched, `fwrite`/`fgetc`/`isalpha`/`_wassert`/etc. were still
   unresolved — the compiler only emits a `--dependent-lib=msvcrt`
   directive, and this toolchain's `msvcrt.lib` doesn't itself forward to
   the actual UCRT/vcruntime import libraries the way a full `cl.exe`
   invocation's implicit default libs would. Fixed with explicit
   `/DEFAULTLIB:ucrt.lib` and `/DEFAULTLIB:vcruntime.lib`, paired with
   `/NODEFAULTLIB:libucrt.lib`/`/NODEFAULTLIB:libvcruntime.lib` to prevent
   the *static* UCRT variants — pulled in by something already in the
   dependency graph — from duplicate-defining the same symbols (`LNK2005`)
   against the dynamic import libs.
3. **Two remaining Win32 API symbols**: `RtlGetLastNtStatus` (`ntdll.lib`)
   and `GetFileVersionInfo*`/`VerQueryValueW` (`version.lib`), added as
   explicit `/DEFAULTLIB` entries.

The resulting `chronicle-codegen.exe` was verified end-to-end, not just
compiled: run against a real header with two `CHRONICLE_TRACKABLE` structs
and one deliberately unmarked struct, confirming the unmarked struct is
skipped and the two `CHRONICLE_TRACK_TYPE(...)` lines it generated compile
and behave correctly when included in an actual program (`track_type()`,
`.get<I>()`, `.set<I>()` all exercised against the generated registrations).

`tools/codegen/CMakeLists.txt` uses `find_package(Clang CONFIG QUIET)` and
`return()`s (skipping the target, not failing the configure) when not
found, gated behind `CHRONICLE_BUILD_CODEGEN` (default `OFF`) in the root
`CMakeLists.txt` — this is the portable path for consumers with a proper
LLVM/Clang install (vcpkg's `llvm` port, a distro dev package, LLVM's own
`install` target elsewhere), which this project's own from-source
development tree does not happen to be. Consumers in that same situation
should build the tool directly, per the link line recorded above, rather
than through CMake.

## Consequences
- Positive: the codegen tool exists, is verified against a real annotated
  header end-to-end, and the marker mechanism (`clang::annotate`, not the
  roadmap's original plain-attribute sketch) is confirmed by direct AST
  inspection rather than assumed to work.
- Positive: the CMake+Ninja+`cl.exe` `TryCompile` bug and its bypass are
  recorded here and are directly reusable for any future CMake-driven build
  on this same toolchain, not just LLVM's.
- Negative: `chronicle-codegen` is not part of the default build and cannot
  be, since most consumers won't have Clang/LLVM dev libraries installed at
  all — `CHRONICLE_BUILD_CODEGEN` defaults `OFF`, and the CMake path itself
  depends on a properly packaged LLVM/Clang install this project's own
  development environment does not have, documented as a manual build
  instead.
- Negative: the from-source LLVM/Clang build only includes the libraries
  `chronicle-codegen` itself needs (not `clang.exe`, not LLVM's own tools),
  so it cannot stand in for a general-purpose LLVM toolchain install if a
  future need arises — a deliberate scope decision to keep the build
  tractable, not an oversight.
