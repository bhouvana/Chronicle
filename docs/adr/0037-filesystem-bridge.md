# ADR 0037: Filesystem Bridge — One Real, Verified Layer 9 Bridge

## Status
Accepted

## Context
[docs/13-vision.md](../13-vision.md)'s Layer 9 asks for network, GPU,
filesystem, and coroutine activity as Chronicle streams, achieved by
*bridging* to real systems — the way the Tracy ([ADR 0013](0013-tracy-bridge.md))
and Perfetto ([ADR 0020](0020-perfetto-export-bridge.md)) bridges already
do — not by Chronicle reimplementing an APM platform. Each bridge is its
own scoped decision. This cycle built and verified exactly one:
**filesystem I/O**, chosen specifically because it needs no external
dependency and no hardware/OS feature this project's actual build/test
environment doesn't already have. GPU and network bridges would need a
real GPU or network stack to verify honestly against — not attempted
here, left as real, separately-scoped future work rather than fabricated
without a way to confirm they work.

## Decision
`include/chronicle/filesystem_bridge.hpp`:
`chronicle::bridges::TrackedFile` wraps a real `std::fstream`. Every
operation (open, read N bytes, write N bytes, close) is recorded as one
`tracked<FileOp>` event — no new recording mechanism, `history()`/
`last_writer()`/etc. all just work on `file.activity()` for free.
Deliberately a small, closed surface (no seek/tell, no `operator<<`/`>>`)
matching this project's "small closed vocabulary" preference elsewhere
(`wire.hpp`'s `WireKind`).

**Not added to the umbrella header**, unlike the PMR allocator adapter
([ADR 0021](0021-pmr-allocator-adapter.md)) — even though `<fstream>` has
no external dependency either, satisfying the same litmus test ADR 0021
used. The distinction: this is conceptually a *bridge* to a specific I/O
concern most consumers won't want pulled into every translation unit that
includes `chronicle.hpp`, not a dependency-free generalization of the core
model the way the PMR adapter is. A deliberate, stated stylistic choice,
not a rule violation.

**A real bug found and fixed while writing the verification test, not by
inspection**: the first version recorded the "Open" event twice — once via
`track()`'s own standing behavior (recording a field's current value as
version 0, so `history()` never starts empty) and once via an explicit
`chronicle::set()` call in the constructor body that assumed `track()`
hadn't already captured it. Fixed by seeding `activity_`'s initial value
*before* calling `track()`, so `track()`'s own version-0 recording *is*
the real Open event, with no second call needed.

### Verification performed
`tests/unit/filesystem_bridge_test.cpp` (2 tests) against a **real
temporary file** (`std::filesystem::temp_directory_path()`, removed before
and after each test) — not a stringstream stand-in, since the entire point
is bridging to the real filesystem: a real write-then-close sequence
correctly produces exactly `Open, Write, Close` in `history()`, with
`is_open()` correctly false afterward; a real write-then-read round trip
across two separate `TrackedFile` instances correctly reproduces the
written bytes (`"abc123"`), with each instance's `history()` showing the
correct `Open, Write/Read, Close` sequence and correct byte counts. Full
suite: **405/405 checks across 102 tests.**

## Consequences
- Positive: a real, working, verified Layer 9 bridge — not a placeholder
  or a claim without a working example behind it.
- Positive: found and fixed a real double-recording bug that a
  stringstream-based or non-file-touching test could plausibly have
  missed if it didn't check exact history length.
- Negative: only one of Layer 9's four named domains (network, GPU,
  filesystem, coroutines) is covered. The other three remain real,
  unattempted future work — each would need its own real system to verify
  against (a GPU context, a network stack, a coroutine executor), which
  this environment either lacks or hasn't been asked to set up.
- Negative: not in the umbrella header — callers must explicitly
  `#include <chronicle/filesystem_bridge.hpp>`, a discoverability cost
  accepted deliberately (see Decision).
