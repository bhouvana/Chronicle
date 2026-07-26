# ADR 0045: vcpkg Port and Conan Recipe (Local, Verified)

## Status
Accepted

## Context
[ADR 0044](0044-installable-package.md) made `chronicle-core` genuinely
installable (`find_package(chronicle CONFIG)`). The next step toward
distributing Chronicle the way established C++ libraries are distributed
is a package manager entry — vcpkg and Conan Center are the two most
relevant registries for a modern header-only C++23 library. Both require
a real, tagged release to fetch: `v2.1.0` was tagged and pushed to
`origin` (`https://github.com/bhouvana/Chronicle`) as the artifact both
package definitions below reference.

## Decision
- **vcpkg port** at `packaging/vcpkg/` (`vcpkg.json` + `portfile.cmake`),
  modeled directly on a real reference port read for structure
  (`entt`'s `portfile.cmake`/`vcpkg.json` in a local vcpkg checkout):
  `vcpkg_from_github` fetching the `v2.1.0` tag, `VCPKG_BUILD_TYPE
  release` (header-only, nothing differs between configurations),
  `vcpkg_cmake_configure` with all four `CHRONICLE_BUILD_*` options
  forced `OFF`, `vcpkg_cmake_config_fixup` relocating the CMake config
  from `lib/cmake/chronicle` to the vcpkg-convention `share/chronicle/`,
  and removal of the now-empty `lib/` tree.
- **Conan recipe** at `packaging/conan/` (`conanfile.py` +
  `test_package/`), the standard Conan Center header-library shape:
  `package_type = "header-library"`, `no_copy_source = True`,
  `package_id()` clearing settings (a header-only package has one
  binary-identical package regardless of compiler/arch/build_type),
  `check_min_cppstd(self, 23)` in `validate()`, and
  `cpp_info.set_property("cmake_target_name", "chronicle::core")` so
  Conan's `CMakeDeps` generator produces the same `chronicle::core`
  spelling as the vcpkg port and direct `find_package` consumers — one
  consistent target name across every distribution path. A
  `test_package/` (Conan Center's mandatory recipe-verification
  convention) builds and runs a real executable against the packaged
  headers.

## Verification performed
Both were built through their real package-manager machinery, not
hand-simulated:
- **vcpkg**: `vcpkg install chronicle --overlay-ports=packaging/vcpkg`
  against the real vcpkg install already on this machine (`D:\vcpkg`).
  First run failed on the deliberate placeholder `SHA512 0` and printed
  the real hash of the `v2.1.0` tarball, which was then filled in; the
  second run fetched the actual pushed tag, configured, built, installed,
  and passed vcpkg's own post-build validation, correctly reporting usage
  as `find_package(chronicle CONFIG REQUIRED)` +
  `target_link_libraries(main PRIVATE chronicle::core)`. Package contents
  inspected directly: 44 files under `include/chronicle`, CMake config
  files correctly relocated to `share/chronicle/`, no leftover `lib/`
  directory. A separate throwaway consumer project was then configured
  with `-DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake`
  (real vcpkg toolchain integration, not just manual
  `CMAKE_PREFIX_PATH`), built, and run — real output: `final value: 3`,
  `history size: 4`, exit code 0.
- **Conan**: Conan 2.31.1 installed (`pip install conan`) and a profile
  detected (`conan profile detect`). `conan create packaging/conan
  --build=missing -s compiler.cppstd=23` (the profile default `cppstd=14`
  correctly failed `validate()`'s check first, confirming that guard
  works) downloaded source from the real pushed
  `github.com/bhouvana/Chronicle/archive/refs/tags/v2.1.0.tar.gz`,
  packaged 36 header files, generated a `chronicle::core` CMake target
  via `CMakeDeps` (confirmed in the log: `Conan: Target declared
  'chronicle::core'`), and ran `test_package`'s real executable — real
  output: `final value: 3`, `history size: 4`.

## Consequences
- Positive: both registry definitions are proven against real package-
  manager machinery, using the real tagged release, not assumed correct
  from reading the recipe/port files.
- Positive: `chronicle::core` is now the confirmed, consistent target
  name across all three consumption paths (direct `find_package`, vcpkg,
  Conan) — the [ADR 0044](0044-installable-package.md) `EXPORT_NAME` fix
  paid off identically in all three.
- Deliberately not yet done: submitting these to the upstream
  `microsoft/vcpkg` and `conan-io/conan-center-index` registries. That is
  a separate, explicit checkpoint — forking and opening PRs against other
  maintainers' repositories is a real, public, hard-to-reverse action
  under this project's GitHub identity with an implied ongoing-
  maintenance commitment, distinct from building and verifying the
  port/recipe locally.
