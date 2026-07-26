# ADR 0047: Fix `std::stacktrace` Link Failure on libstdc++ (GCC/Clang+libstdc++)

## Status
Accepted

## Context
[ADR 0046](0046-fix-derive-gcc-clang-portability.md) fixed the `derive()`
compile error that had been blocking `ubuntu-gcc`/`ubuntu-clang` CI.
Pushing that fix and re-checking the real CI run (not assumed --
`gh`/API-polled until `completed`) showed both Linux jobs now progress
past compilation and fail at **link** time instead:

```
undefined reference to `__glibcxx_backtrace_simple'
undefined reference to `__glibcxx_backtrace_create_state'
```
(`ubuntu-gcc`, GCC 13 headers), and
```
undefined reference to `std::stacktrace_entry::_Info::_M_populate(unsigned long)'
undefined reference to `std::__stacktrace_impl::_S_current(...)'
```
(`ubuntu-clang`, which picked up **GCC 14** libstdc++ headers on the same
"ubuntu-latest" runner image -- the two Linux jobs are not on the same
libstdc++ version despite both being "ubuntu-latest").

`provenance.hpp` ([ADR 0032](0032-provenance-stacktrace.md)) uses
`std::stacktrace`. On libstdc++, its backing implementation lives in a
*separate* static library that must be explicitly linked, and its name
changed across GCC versions: `stdc++_libbacktrace` for GCC \<= 13,
`stdc++exp` for GCC 14+. This was never caught locally because neither
compiler available in this environment (MSVC, and Windows-hosted Clang
targeting the MSVC ABI/MSVC STL) needs either library -- MSVC STL's
`std::stacktrace` links unaided.

## Decision
Detect the requirement with a real compile+link check
(`CheckCXXSourceCompiles`) in `src/CMakeLists.txt`, rather than
branching on compiler ID or a hardcoded GCC version cutoff -- exactly
because the two real CI jobs demonstrated that "which GCC version" isn't
reliably inferable from "which compiler frontend" or "which OS image":

1. Try compiling+linking a minimal `std::stacktrace::current()` program
   unaided. If that succeeds (MSVC STL, and presumably libc++ builds that
   don't need this), do nothing.
2. Otherwise try again with `stdc++exp` linked; if that succeeds, add it
   to `chronicle-core`'s `INTERFACE` link libraries.
3. Otherwise try `stdc++_libbacktrace`; if that succeeds, add it instead.

Attached to the `chronicle-core` `INTERFACE` target itself (not just
`tests/unit/CMakeLists.txt`) so the requirement propagates to every real
consumer automatically -- the exported `ChronicleTargets.cmake`
([ADR 0044](0044-installable-package.md)) carries it too, meaning a real
`find_package(chronicle CONFIG)`/vcpkg/Conan consumer on Linux gets the
correct link flag without knowing this internal detail exists.

## Verification performed
Re-confirmed locally that the check correctly no-ops on both Windows
compilers available in this environment: `CHRONICLE_STACKTRACE_LINKS_UNAIDED`
reports `Success` on both MSVC and Windows-hosted Clang 21.1.6, and full
rebuilds pass unchanged on both (MSVC 446/446, Clang 408/408) -- proving
this change is inert where it isn't needed. The GCC-13/GCC-14 library-name
branches cannot be exercised on this Windows-only environment; real
confirmation is the next real CI run on `ubuntu-gcc`/`ubuntu-clang` after
this commit, checked the same way the link failure itself was found (via
the real Actions API, not assumed).

## Consequences
- Positive: fixes a real link failure that would have hit every Linux
  consumer of `chronicle-core` (not just this project's own CI) --
  anyone `find_package(chronicle)`-ing on Linux and using any code path
  that includes `provenance.hpp` would have hit the identical undefined-
  reference error.
- Positive: self-adapting across GCC versions via a real compile check,
  not a hardcoded version cutoff that could go stale on the next GCC
  release.
- Process note, same as ADR 0046: this was only found by actually
  watching a real CI run to completion after the first fix, rather than
  assuming one fix meant "CI is green now."
