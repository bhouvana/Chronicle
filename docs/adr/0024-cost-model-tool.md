# ADR 0024: Cost-Model Estimator for Tracked Scalar Fields

## Status
Accepted

## Context
[docs/12-future-research-topics.md](../12-future-research-topics.md) topic 4
asks for a tool that turns [09-performance.md](../09-performance.md)'s
"measure after the fact" philosophy into "predict before you commit to
tracking this field": given an expected write frequency, project a tracked
field's hot-path and storage cost. `bench/baseline.json` and
`bench/compare_baseline.py` ([ADR 0017](0017-ci-performance-gate.md))
already establish this project's precedent for cost claims — real,
measured numbers with an honest tolerance, not projected/theoretical ones.

## Decision
`bench/cost_model.py`, a sibling to `compare_baseline.py`: given
`--writes-per-sec`, `--duration-s`, `--threads`, `--causal-clock`, and a
retention policy (`--ring-window N` or `--unbounded`), reports (1) hot-path
cost by looking up the matching real `bench/baseline.json` key
(`tracked_assignment_*_single_threaded` or `contended_record_N_threads_aggregate`,
whichever the requested thread count/causal-clock combination actually has
a measurement for — refuses to guess at a combination with no real
baseline, e.g. contended + causal_clock, which chronicle-bench doesn't
measure yet), and (2) on-disk storage footprint using
`BASE_BYTES_PER_EVENT = 81.082`, measured directly (not assumed) by writing
1000 real `tracked<int>` mutations through the real `SessionWriter` to an
actual file and dividing file size by event count, plus a fixed
`+16 bytes` when `causal_clock` is modeled — not measured empirically,
since `chronicle/io/format.hpp`'s `write_hlc()` is an unconditional
`write_u64 + write_u64` with no variable-length component, verified by
reading the function directly.

**Explicitly scoped to `tracked<T>` scalars only.** `tracked_vector<T>`/
`tracked_map<K,V>` have per-operation payloads of genuinely variable size
(an inserted string, a container element) — reducing that to one constant
without knowing the caller's actual payload shapes would be a fabricated
number, not a measured one. Extending this tool to containers is real,
scoped future work with its own measurement methodology, not attempted
here.

### Verification performed
Ran directly against the real `bench/baseline.json`: 1000 writes/sec over
60s with `ring_window(1024)` correctly reports the window-capped footprint
(~81 KiB, flags that 60,000 events exceed the 1024-event window); 500
writes/sec over 1 hour with `causal_clock` + `unbounded` correctly reports
the 97.08 bytes/event (81.082 + 16) and an unbounded-growth warning. The
contended+causal_clock combination (no real baseline exists for it) exits
non-zero with an explicit refusal rather than silently estimating.

## Consequences
- Positive: cost estimates come entirely from real measurements already in
  the repo (or a directly-read format constant), consistent with this
  project's "measure, don't assume" bar — no new benchmark infrastructure,
  no new opt-in build flag, no C++ code at all.
- Positive: refuses rather than guesses when a requested configuration has
  no real baseline data — an explicit gap in `chronicle-bench`'s own
  coverage, not silently papered over.
- Negative: `BASE_BYTES_PER_EVENT` is a single point measurement (one
  session, one field, one thread) — like every number in `bench/RESULTS.md`,
  it inherits that file's documented noise caveats and would benefit from
  repeat runs if this tool's numbers start mattering for a real decision.
- Negative: scalar-only; container fields (the more storage-heavy case in
  practice) are out of scope until a real per-container-op measurement
  methodology exists.
