# ADR 0044: Installable Package (License, Versioning, `install()`/Export)

## Status
Accepted

## Context
Chronicle had no `LICENSE` file, no `install()`/export rules anywhere in
the CMake, `project(chronicle VERSION 0.1.0 ...)` still frozen despite
the v2.0+ feature set actually shipped, no top-level-project guard (a
consumer doing `FetchContent_Declare`/`add_subdirectory` would force-build
Chronicle's own tests/examples/bench/CLI into their build), and zero git
tags. These block every distribution path simultaneously: direct
`FetchContent` consumption, a real `find_package(chronicle CONFIG)`
workflow, and registry submission to vcpkg or Conan Center all require
some or all of the above.

## Decision
- **License**: Apache License 2.0 (`LICENSE`, root). Permissive,
  patent-grant-bearing, and the de facto default for C++ infrastructure
  libraries this project is positioning itself alongside.
- **Version**: `project(chronicle VERSION 2.1.0 ...)`. An honest MINOR
  bump from the last-declared `0.1.0` — everything shipped since (the 8
  research topics, the 10-layer vision, this session's platform-primitives
  round) was additive; nothing broke the API surface committed to in
  [ADR 0018](0018-v1-api-stability-commitment.md).
- **Top-level-project guard**: `CHRONICLE_IS_TOP_LEVEL` is set from
  `CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR` and used as the
  default for `CHRONICLE_BUILD_TESTS`/`_EXAMPLES`/`_BENCH`/`_TOOLS`
  (`ON` only when Chronicle is the top-level project, `OFF` when pulled
  in via `add_subdirectory`/`FetchContent`). A consumer no longer gets
  Chronicle's own test suite, examples, benchmarks, or CLI force-built.
- **Install/export, scoped to `chronicle-core` only**: `install(TARGETS
  chronicle-core EXPORT ChronicleTargets)` and `install(DIRECTORY
  include/ ...)` in `src/CMakeLists.txt`; `install(EXPORT ...)` plus a
  generated `ChronicleConfig.cmake`/`ChronicleConfigVersion.cmake` (via
  `CMakePackageConfigHelpers`, `SameMajorVersion` compatibility) in the
  root `CMakeLists.txt`, all gated behind `CHRONICLE_IS_TOP_LEVEL` so a
  consumer embedding Chronicle via `add_subdirectory` doesn't get
  Chronicle's install rules merged into their own install tree.
  Deliberately **not** installing the opt-in tools/adapters/bridges
  (CLI, EnTT adapter, Tracy bridge, memcpy shim) in this pass — those
  have their own heavier dependency graphs and are real, separate future
  scope, not needed for the header-only `chronicle::core` target that
  `find_package` consumers actually want.

## Verification performed
Full `chronicle-core-tests` suite rebuilt from a clean reconfigure
(NMake Makefiles, MSVC 19.44) after these changes and still passes
**446/446 checks across 115 tests** — the guard and install rules do not
alter this repo's own default local-build behavior. (`find_package`
consumer-project verification and vcpkg/Conan local builds are tracked as
separate, subsequent steps — this ADR covers the CMake surface only.)

## Consequences
- Positive: unlocks `find_package(chronicle CONFIG)`, vcpkg, and Conan
  Center submission simultaneously — all three needed exactly these
  prerequisites.
- Positive: no behavior change for this repo's own build (verified via
  full test suite re-run), since the guard preserves `ON` defaults when
  Chronicle is top-level.
- Negative: only `chronicle-core` is installable for now; consumers
  wanting the CLI or adapters still need a source build. Acceptable —
  those have real external dependencies (EnTT, Tracy, Detours) that
  belong in a separate packaging pass, not bundled into unblocking the
  common case.
