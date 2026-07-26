# ADR 0048: Fix Two Real Test Bugs Surfaced by Linux CI (Race Thread-ID, Provenance Tail-Call)

## Status
Accepted

## Context
After [ADR 0046](0046-fix-derive-gcc-clang-portability.md) and
[ADR 0047](0047-fix-stacktrace-linking-on-libstdcxx.md) fixed the compile
and link failures blocking `ubuntu-gcc`/`ubuntu-clang` CI, the real CI run
progressed all the way to actually *running* the test suite for the first
time in this investigation -- and surfaced two more real, previously-
invisible bugs (invisible because the suite had never successfully linked
on Linux before):

1. `race_test.cpp`'s `possible_race_is_false_for_cross_thread_events_far_apart_in_time`
   asserted `rec_a.thread_id != rec_b.thread_id` after `t1` was spawned,
   run, and **joined**, then (50ms later) `t2` was spawned and joined.
   Because the two threads' lifetimes never overlap, a real OS is free to
   recycle the exited thread's id for the new thread -- which is exactly
   what happened on the real Linux runner. The sibling test
   (`..._sharing_a_physical_tick`) keeps an equivalent thread-id check and
   is correct, because there both threads are alive simultaneously (kept
   as-is).
2. `provenance_test.cpp`'s
   `provenance_does_not_cross_contaminate_between_fields_with_colliding_versions`
   asserted `trace_a->size() > trace_b->size()`, relying on `middle_write()`
   contributing its own stack frame on top of `inner_write()`'s. `noinline`
   only forbids *inlining* -- a separate optimization, tail-call
   elimination, can still turn `middle_write`'s trailing call to
   `inner_write()` into a sibling jump, which removes `middle_write`'s
   frame from the real captured stack at runtime (a *correct* unwinder
   report, given TCO actually happened -- not a bug in the stacktrace
   capture itself). This is genuinely optimization-level-dependent: it
   passed locally at `-O2` and failed on real CI's `-O3` `Release` build.

## Decision
- **Race test**: removed the `thread_id` inequality assertion from the
  "far apart in time" test (kept in the "sharing a physical tick" test,
  where it's actually sound). The real thing that test verifies -- the
  time-window logic of `possible_race()` -- is unaffected and still
  checked.
- **Provenance test**: added `CHRONICLE_TEST_DEFEAT_TAIL_CALL()` (an empty
  `asm volatile("" ::: "memory")` on non-MSVC, a no-op on MSVC, which
  doesn't need it) after `middle_write`'s call to `inner_write`, forcing
  the compiler to treat the call as not being in tail position, so its
  frame reliably survives in the captured trace regardless of
  optimization level.

## Verification performed
Neither failure is reproducible with the compilers available in this
environment at the optimization levels tried first (`-O2`
`RelWithDebInfo`), which is exactly why they were invisible until the
real Linux CI run reached this point. Reconfigured the local Windows-
hosted Clang 21.1.6 build as `Release` (`-O3 -DNDEBUG`, matching CI's
actual flags exactly) specifically to try to reproduce -- confirmed both
fixed tests now pass cleanly under real `-O3` optimization (`405/407`,
with the only 2 remaining failures being the already-documented
Windows-PDB-vs-DWARF `source_line` artifact from cross-testing a Windows-
hosted compiler, unrelated to native Linux CI). Real confirmation is the
next real CI run.

## Consequences
- Positive: two genuine bugs fixed -- one a real race-condition-adjacent
  test bug (asserting an invariant that isn't actually guaranteed), one a
  real optimization-level portability gap in a stacktrace test.
- Process note, continuing ADR 0046/0047: every fix in this investigation
  was found by reading real CI logs and confirmed by reproducing the
  underlying compiler behavior locally wherever possible (a standalone
  repro for the `derive()` bug, a `Release`-mode local Clang run for
  these two) -- never assumed fixed from source inspection alone.
