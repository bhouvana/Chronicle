# ADR 0049: Fix `find_package(chronicle CONFIG)` Case-Sensitivity Bug

## Status
Accepted

## Context
After opening the real `microsoft/vcpkg` PR (#53061), an automated Copilot
code review caught a real bug: [ADR 0044](0044-installable-package.md)'s
generated CMake config files were named `ChronicleConfig.cmake` /
`ChronicleConfigVersion.cmake` (capital C), but every doc/example in this
project calls `find_package(chronicle CONFIG)` -- lowercase, matching
`project(chronicle ...)`. CMake's Config-mode search constructs the
filename to look for using the *exact case* of the name argument passed
to `find_package()`; it does not case-fold. On Windows and default macOS
volumes (case-insensitive filesystems), the mismatch is invisible -- which
is exactly why every consumer-project verification this session had
already done (direct `find_package`, the vcpkg overlay-port install
through the real vcpkg toolchain, the Conan recipe) passed cleanly: all
of it ran on this Windows development machine. On any case-sensitive
filesystem (most Linux distributions, an opt-in macOS volume format),
`find_package(chronicle CONFIG)` would silently fail to find the package
at all.

## Decision
Renamed the generated config files' installed names from
`ChronicleConfig.cmake`/`ChronicleConfigVersion.cmake` to
`chronicleConfig.cmake`/`chronicleConfigVersion.cmake` (root
`CMakeLists.txt`, the `configure_package_config_file`/
`write_basic_package_version_file`/`install(FILES ...)` calls).
`ChronicleTargets.cmake` (capital C) is deliberately left unchanged: it is
never looked up by `find_package()`'s own case-sensitive name-construction
rule -- it's only `include()`'d from within this project's own generated
`chronicleConfig.cmake` with a hardcoded, self-consistent path, so its
case is a purely internal detail.

## Verification performed
Reproduced the actual failure mode directly on a real case-sensitive
filesystem before fixing anything: WSL's ext4 volume, `touch
ChronicleConfig.cmake` then `test -f chronicleConfig.cmake` -- genuinely
not found, confirming this wasn't a theoretical concern. After the fix,
reconfigured and reinstalled locally and confirmed the actual generated
filenames are now lowercase (`chronicleConfig.cmake`,
`chronicleConfigVersion.cmake`), then re-ran the exact same WSL
case-sensitivity check against the new lowercase name -- found. Re-ran
the full local verification chain to confirm nothing else broke: the
throwaway consumer project (real `find_package` + `chronicle::core` link)
still builds and runs correctly (`final value: 3`, `history size: 4`),
and the full `chronicle-core-tests` suite still passes **445/445**.

## Consequences
- Positive: fixes a real bug that would have affected *every* Linux/case-
  sensitive-filesystem consumer of `chronicle::core` via `find_package`,
  vcpkg, Conan, or plain `cmake --install` -- not specific to the vcpkg
  port, caught there only because that PR's CI happened to run on Linux
  first.
- Process note: this is the second time in this packaging effort that a
  platform this development environment cannot directly exercise (Linux
  CI, and now a case-sensitive filesystem) surfaced a real bug invisible
  to every local check. Real, independent review (an external PR's CI and
  automated review, not just this project's own local verification) is
  doing real work here -- exactly the reason to open the PRs rather than
  stop at "verified locally."
