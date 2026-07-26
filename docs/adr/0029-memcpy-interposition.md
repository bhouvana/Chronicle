# ADR 0029: Address-Range-Filtered `memcpy` Interposition (Windows)

## Status
Accepted (Windows); Linux untested — see Consequences

## Context
[docs/12-future-research-topics.md](../12-future-research-topics.md) topic 3's
post-v2.0 spike built a real, running Detours-based `memcpy` hook on
Windows and found two things: compile-time-constant-size copies (the
common `tracked<T>` scalar case) are structurally invisible to any
libc-level hook (MSVC `/O2` inlines them into raw load/store instructions
with zero reference to the `memcpy` symbol at all); and a second,
unprompted internal CRT call also triggered the unfiltered hook, meaning a
process-wide interposition catches everything, not just tracked memory —
"a real deployment would need to filter hook firings against the address
ranges of live `tracked<T>`/... instances... real additional scope, not
attempted in this spike."

## Decision
Two new pieces close that specific, previously-identified gap:

1. `include/chronicle/interposition_registry.hpp` — a small, header-only,
   zero-dependency `Registry` (mutex-guarded vector of address ranges) plus
   `chronicle::interposition::watch(tracked<T> const&)`/`unwatch(...)`
   convenience wrappers using `tracked<T>::get()`'s already-public accessor
   (no new method needed on `tracked<T>` itself, no change to its hot
   path). Always available, costs nothing unless a caller explicitly
   calls `watch()`.

2. `tools/memcpy-shim/` — the actual Detours-based hook, gated behind a
   new `CHRONICLE_BUILD_MEMCPY_SHIM` option (default `OFF`), using
   `find_path`/`find_library` rather than `find_package(... CONFIG)`:
   verified directly that Detours' vcpkg port has no CMake config package
   (no `*-config.cmake` under its `share/` directory, unlike zstd/EnTT/
   httplib), so it follows Detours' own documented vcpkg usage pattern
   instead. `DetourMemcpy` checks `Registry::instance().overlaps(dst, size)`
   before counting a "watched" hit — the filtering the original spike
   didn't have.

**Real build-time finding**: Detours' `detours.h` requires the Windows
SDK's own `_AMD64_`/`_X86_`/etc. macros (defined by `<windows.h>` based on
the compiler's `_M_X64`/`_M_IX86`), not just the compiler-level macros
directly — building without including `<windows.h>` first produces a real
`#error Unknown architecture`, found by actually building this, not
anticipated from Detours' documentation.

### Verification performed
`tests/unit/interposition_registry_test.cpp` (4 tests, no Detours needed):
range overlap detection, partial-overlap-at-boundary correctness,
unwatch stops reporting, and the `tracked<T>` convenience wrapper.

`tools/memcpy-shim/demo.cpp` — a real, running end-to-end demonstration,
built and executed (not just compiled): watches one `tracked<int>` field,
installs the real hook, then performs three raw `memcpy` calls — (1) a
compile-time-constant `sizeof(int)` into the watched field's storage, (2)
a genuinely runtime-variable size (derived from `argc`, unfoldable) into
the same watched storage, (3) the same runtime-variable size into an
unrelated, unwatched buffer. **Actual output: `total_calls=2
watched_hits=1 final_value=555`.** This confirms both halves honestly in
one real run: only 2 of the 3 memcpy calls ever reached the real symbol at
all (case 1's compile-time-constant copy was fully inlined away, exactly
reproducing the original spike's structural finding — address filtering
cannot and does not fix this), and of those 2, exactly 1 was correctly
attributed to the watched range (case 2) while case 3's unwatched
destination was correctly excluded — the actual, new address-filtering
capability this ADR adds. `final_value=555` additionally confirms the raw
write really did land in the tracked field's storage while completely
bypassing `tracked<T>::operator=`/`chronicle::set()` — the exact blind
spot this whole topic exists to partially close.

## Consequences
- Positive: the specific gap the original spike named as real, additional,
  not-yet-attempted scope is now closed and verified with a real run, not
  just implemented.
- Positive: `Registry` is reusable by any future interposition mechanism
  (Linux LD_PRELOAD, a different platform's DBI hook) without
  reimplementing address bookkeeping — the same "mechanism exists" note
  the original spike made about the PMR adapter, now actually factored out.
- Negative (inherited, not introduced): the compile-time-constant-size
  blind spot is structural and unfixable by this or any address-filtering
  approach — reconfirmed, not solved, exactly as honestly as the original
  spike stated it.
- **Negative, stated plainly**: `tools/memcpy-shim/linux/memcpy_preload.cpp`
  is real, standard `dlsym(RTLD_NEXT, ...)`-based LD_PRELOAD C++ — but it
  has **not been built or run anywhere**. This tool-execution environment
  is Windows-only with no Linux available, the same kind of environmental
  wall the VS Code extension's Electron GUI hit
  ([ADR 0022](0022-vscode-extension.md)). Offered as a real, reviewable
  starting point, explicitly not a verified one; anyone building it on
  real Linux should verify it against the same three-case demo pattern
  `tools/memcpy-shim/demo.cpp` already used successfully on Windows before
  relying on it.
- Negative: still not recommended as an always-on feature — this remains
  an opt-in, heavyweight diagnostic tool (`CHRONICLE_BUILD_MEMCPY_SHIM`
  default `OFF`), consistent with the original spike's cost/value
  conclusion, which this ADR does not revisit.
