# ADR 0046: Fix `chronicle::derive()` GCC/Clang Portability Bug

## Status
Accepted

## Context
GitHub Actions CI (`ubuntu-gcc`, `ubuntu-clang`) had been failing since
before this session's packaging work (confirmed back to commit
`fab46344`) while `windows-msvc` stayed green — a real, pre-existing
correctness gap that predates [ADR 0044](0044-installable-package.md)/
[0045](0045-vcpkg-and-conan-packaging.md), not something they introduced.
Every failure traced to one call shape: `chronicle::derive<Result,
Deps...>(target, lambda, deps...)`.

`derive()`'s signature took the callable directly as
`std::function<Result(Deps const&...)> compute`. GCC and Clang apply the
class-template-deduction rule (`[temp.deduct.call]`) to any parameter
whose type is a class template specialization: the *argument* must
literally be (or derive from) that specialization -- implicit/user-
defined conversions are not considered during deduction. A plain lambda
is never "derived from" `std::function<...>`, so deduction for that one
parameter failed outright -- even though `Result` and every `Deps` were
already fully pinned by explicit template arguments
(`derive<int, int, int>(...)`). MSVC accepts the call anyway (a known
MSVC leniency, not standard-conforming here); GCC and Clang correctly
reject it. This is why the bug was invisible on `windows-msvc` CI and on
every local build this project had used (MSVC, plus Windows-hosted Clang
only ever exercised via `-fsyntax-only` spot checks, not a full
Linux-shaped CI run) -- it was never actually caught until this session's
CI-log investigation.

## Decision
Give the callable its own freely-deduced template parameter `Fn`,
positioned *after* the `Deps...` pack in `derive()`'s template parameter
list:

```cpp
template <typename Result, typename... Deps, typename Fn>
[[nodiscard]] std::unique_ptr<derived::Derivation<Result, Deps...>> derive(
    tracked<Result>& target, Fn compute, tracked<Deps>&... deps) {
    return std::make_unique<derived::Derivation<Result, Deps...>>(
        target, std::function<Result(Deps const&...)>(std::move(compute)), deps...);
}
```

`Fn` after a pack is legal here specifically because it is *never*
explicitly specified by any caller -- every call site only ever writes
`derive<Result, Deps...>(...)`, which fixes `Result`/`Deps` completely;
`Fn` is deduced purely from the actual `compute` argument, same as any
ordinary function template parameter. The conversion to the concrete
`std::function<Result(Deps const&...)>` now happens as an ordinary
(non-deduced) constructor call inside the function body, where implicit
conversions are unconditionally allowed -- sidestepping
`[temp.deduct.call]`'s class-template-specialization rule entirely. Zero
change to the public call convention: `derive<int, int, int>(target,
lambda, dep1, dep2)` compiles identically to before, for every caller.

`chronicle::watch()` ([rules.hpp](../../include/chronicle/rules.hpp))
uses a superficially similar `std::function<...>`-typed parameter but was
confirmed **not** to have this bug (both real GCC and Clang CI logs show
`watch<int>(...)` compiling cleanly) -- its template parameter `T` is a
single type fully deducible from the first parameter (`tracked<T>&
field`) alone, not a pack split across two non-adjacent parameter
positions the way `derive()`'s `Deps` was. Left unchanged.

## Verification performed
- Minimal standalone repro (`Box<Result, Deps...>` mirroring `Derivation`)
  reproduced the exact GCC diagnostic ("is not derived from
  std::function<...>") against real local Clang 21.1.6, and confirmed the
  `Fn`-parameter fix resolves it -- before touching the real header.
- `derived.hpp`'s actual fix applied, then `-fsyntax-only` against the
  real `derived_test.cpp`, `rules_test.cpp`, and `io_test.cpp` (the three
  files that failed in the real CI log): clean, zero errors.
- Full real build+link+run with Clang 21.1.6 (Windows-hosted, targeting
  the MSVC ABI, RelWithDebInfo -- `-Xclang -gcodeview` needed purely for
  this cross-testing setup's PDB-based `std::stacktrace` resolution, a
  local-environment detail, not a code change): **408/408 checks pass**.
  (An intermediate Debug-mode run surfaced 8 unrelated
  `tracked_memory_resource_test.cpp` failures caused by MSVC STL's
  `_ITERATOR_DEBUG_LEVEL=2` debug allocator bookkeeping changing
  allocation counts/sizes -- a debug-vs-release testing artifact from
  defaulting to the wrong `CMAKE_BUILD_TYPE`, not a real bug; resolved by
  matching `Release`.)
- Full MSVC rebuild + `chronicle-core-tests` re-run after the fix: still
  **446/446 checks pass** (446 vs. Clang's 408 is the expected difference
  from optional Zstd/EnTT-adapter tests only available in the MSVC build
  via vcpkg, not a regression).

## Consequences
- Positive: fixes a real, previously undetected cross-compiler
  correctness bug in a documented, "shipped" public API
  ([docs/13-vision.md](../13-vision.md) Layer 4). Anyone building
  Chronicle with GCC or Clang and calling `derive()` was completely
  blocked before this fix.
- Positive: zero call-site changes required -- purely an internal
  signature restructuring.
- Process note: this was caught only by actually reading the real CI logs
  for a repo that had been assumed green -- a reminder that "the local
  MSVC/Clang builds pass" is not equivalent to "CI is green," and this
  project's own "verify for real, don't assume" standard applies to CI
  health, not just to individual features.
