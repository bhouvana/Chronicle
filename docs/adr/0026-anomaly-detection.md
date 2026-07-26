# ADR 0026: Online Range-Anomaly Scoring for Tracked Scalars

## Status
Accepted

## Context
[docs/12-future-research-topics.md](../12-future-research-topics.md) topic 8
asks for flagging "this field's value is outside its historically observed
range/rate of change" once `StateStream`s exist as a queryable corpus — but
explicitly warns of "a real risk of becoming a research distraction" and,
per [docs/02-competitive-gap-analysis.md](../02-competitive-gap-analysis.md)'s
security-research audience skepticism, any such feature inherits this
project's blind-spot ceiling and honest-claim bar
([04-technical-limitations.md](../04-technical-limitations.md)): it can
only reason over what was actually instrumented, and must never be
marketed as more than it is.

## Decision
`include/chronicle/anomaly.hpp`: `chronicle::range_anomalies(Timeline<T> const&, z_threshold = 3.0, min_samples = 3)`,
arithmetic `T` only (`static_assert`, not a runtime check — scoring a
`std::string` or struct against a numeric z-score isn't a meaningful
operation, not a missing feature). Uses Welford's online mean/variance
algorithm, deliberately **causal**: each event is scored only against the
running statistics of events strictly before it, never including itself or
future events. This is the one design choice that makes the "machine-
assisted" framing honest rather than a hindsight trick — a whole-history
batch z-score can silently average an early outlier back into "normal" once
enough later data arrives, which is not what a live monitor watching a
running program actually sees. `min_samples` (default 3) withholds scoring
until there's enough running history to make a stddev meaningful at all,
not a tuned threshold — an event before that point isn't "not anomalous,"
it's un-scoreable.

**"Machine-assisted" means real statistical scoring, not a trained ML
model** — stated explicitly here because docs/12 titles this topic
"machine-assisted anomaly detection" and this project has a standing
policy (04-technical-limitations.md) against implying more precision or
sophistication than a mechanism actually has.

Zero-variance handling: when every prior sample is identical (stddev == 0),
a z-score divides by zero. Rather than skip scoring, a differing value in
that state is flagged directly (a real, unambiguous "outside observed
range" case a z-score literally cannot express) — this is a correctness
fix, not a heuristic tweak, found while writing the first version of this
function against a real all-constant test case.

Scalar-only (`tracked<T>` via `Timeline<T>`), matching this project's
established pattern for new features (ADR 0024's cost-model tool is
likewise scalar-first) — `tracked_vector`/`tracked_map`'s structural ops
have no single numeric value to score against a running mean at all;
anomaly detection over container *shape* (sudden size changes, unusual
insert/erase rates) is a real, different, future-scoped question.

### Verification performed
`tests/unit/anomaly_test.cpp` (5 tests): below-`min_samples` events are
never flagged; a real numeric outlier against ten stable, low-noise values
is correctly flagged; a field that never changes produces zero anomalies;
an all-identical-then-different sequence is flagged via the zero-variance
path without a division-by-zero; and — the test that actually exercises
the causal design choice — an early outlier followed by 30 "normal" values
is still flagged, which a whole-history batch computation could have
washed out. Full suite: **272/272 checks across 63 tests**.

## Consequences
- Positive: a real, working, honestly-scoped answer to docs/12 topic 8,
  using a well-understood, numerically stable algorithm (Welford's), not a
  novel or unverified statistical claim.
- Positive: additive to the ADR 0018-covered public surface.
- Negative: inherits every blind spot this project's source-level
  instrumentation already has (04-technical-limitations.md) — an anomaly
  in an untracked field, or one caused by a raw-memory write (topic 3),
  is invisible to this the same way it's invisible to everything else.
- Negative: `z_threshold`/`min_samples` are tunable parameters, not
  universally "correct" values — like every other cost/threshold number in
  this project, they're a starting point a caller should validate against
  their own field's real behavior, not a guarantee.
