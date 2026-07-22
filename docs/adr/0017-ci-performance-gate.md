# ADR 0017: CI Performance Gate Uses a Deliberately Loose Tolerance Against a Dev-Machine Baseline

## Status
Accepted

## Context
[10-roadmap.md](../10-roadmap.md)'s v1.0 scope calls for "CI performance-
regression gate enforced on the benchmark suite (Phase 9) as a hard merge
requirement." `bench/baseline.json` already existed as a "seed... not an
enforced gate," captured on this project's own dev workstation
([ADR 0009](0009-lock-free-ring-buffer.md)'s numbers) with its own
documented caveat: "single machine, single compiler, unisolated
environment... not the CI regression baseline."

Two real constraints shaped this gate, both measured rather than assumed:

- **This project's own benchmarking already found ~30-50% run-to-run
  variance from ambient system noise alone**, on the *same* dev machine,
  *same* binary, run consecutively (the Tracy-bridge hook-cost A/B in
  [ADR 0013](0013-tracy-bridge.md) and `bench/RESULTS.md`'s own
  Interpretation section). Re-running `chronicle-bench` locally while
  writing this gate reproduced the same thing: `history_query_100000_events`
  and `tracked_assignment_*` swung +6% to +78% across consecutive runs, with
  no code change at all.
- **`bench/baseline.json` was captured on a different machine than any CI
  runner will ever run on.** GitHub Actions' shared/virtualized runners are
  typically *noisier* than a dedicated dev workstation, not quieter (shared
  vCPUs, no isolation guarantees). A tolerance tight enough to mean
  something on identical hardware would fail nearly every CI run here on
  noise, not signal — which would either get the gate disabled entirely
  (worse than no gate) or trained-to-ignore by whoever's merging (also
  worse than no gate).

## Decision
`chronicle-bench` gained a `--json` flag (`bench/main.cpp`) emitting
`{"results_ns_per_op": {...}}` with keys matching `bench/baseline.json`
exactly — both were written to agree on names, not reconciled after the
fact. `bench/compare_baseline.py` loads a fresh run and the baseline,
computes each benchmark's percent delta, and fails (non-zero exit) if any
exceeds `--tolerance` (default **100%, i.e. fails only past 2x slower**).
Chosen directly from the measured noise above, not picked arbitrarily: a
tolerance below roughly that range would have failed the *verification run
of this very gate*, on this project's own reference machine, with zero
regressing code change. This gate's job is catching gross regressions (an
accidental `O(n)` → `O(n²)`, a reintroduced lock on the hot path) — not a
substitute for `bench/RESULTS.md`'s own honest, human-reviewed numbers for
anything subtler than 2x.

`.github/workflows/bench.yml` builds `chronicle-bench` alone (`
-DCHRONICLE_BUILD_TESTS=OFF -DCHRONICLE_BUILD_EXAMPLES=OFF
-DCHRONICLE_BUILD_TOOLS=OFF`, no external dependencies needed) on
`windows-latest`, runs it with `--json`, and runs
`compare_baseline.py` against it — a required check on every push/PR to
`main`. `.github/workflows/ci.yml` separately builds and runs the full unit
suite across three real configurations (`windows-msvc`, `ubuntu-gcc`,
`ubuntu-clang` — not the full `{gcc,clang,msvc} x {linux,macos,windows}`
matrix [11-repository-structure-and-standards.md](../11-repository-structure-and-standards.md)
sketches; macOS is left out honestly as untested rather than claimed
working without having checked it anywhere).

### Verification performed
Every step in both workflow files was run locally against a clean
out-of-source build directory before pushing — not just "the YAML looks
right": `cmake -B build -DCMAKE_BUILD_TYPE=Release` (confirmed a
multi-config generator is selected here, matching what a Visual-Studio-
equipped `windows-latest` runner does, and that `CMAKE_BUILD_TYPE`'s
resulting "unused variable" warning is expected/harmless for that case),
`cmake --build build --config Release`, `ctest --test-dir build -C
Release` (passed), and separately the bench-only configure/build producing
`build/bench/Release/chronicle-bench.exe` (the exact path `bench.yml`
references), piping `--json` output into `compare_baseline.py` against the
real `bench/baseline.json`, and confirming both a clean pass (real noisy
data, +6% to +78% deltas, all under the 100% gate) and a real failure
(an artificially injected 500ns/op regression correctly flagged and exited
non-zero). All of this ran before any of it was pushed.

## Consequences
- Positive: a real, working, verified regression gate exists where none
  did before — catches the class of bug this project has already had
  direct experience with needing a benchmark to surface (ADR 0004's
  original ~10x-over-budget finding, ADR 0009's honest single-threaded
  regression).
- Positive: the gate's tolerance is derived from this project's own
  measured noise floor, not a guessed number — consistent with
  `bench/RESULTS.md`'s standing "measure, don't assume" discipline applied
  to the gate itself, not just the benchmarks it watches.
- Negative: a 100% tolerance will not catch a real 20-50% regression — a
  known, explicit limitation, not a false sense of security. Revisit once a
  CI-native baseline (captured on the runner itself, across multiple runs,
  with its own noise profile characterized) exists to replace the
  dev-machine one.
- Negative: the CI build/test matrix (`ci.yml`) covers three
  configurations, not docs/11's full aspirational matrix — macOS
  specifically is an acknowledged gap, not silently assumed fine.
- Follow-on: once this gate has run for real on `main` a few times, revisit
  the tolerance with actual CI-runner noise data instead of the dev-machine
  extrapolation used to pick it here.
