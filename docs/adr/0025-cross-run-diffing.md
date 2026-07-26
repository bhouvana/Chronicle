# ADR 0025: `chronicle-cli diff-runs` for Cross-Run/Cross-Session Diffing

## Status
Accepted

## Context
[docs/02-competitive-gap-analysis.md](../02-competitive-gap-analysis.md)
names cross-run diffing ("comparing two runs of the same scenario") as a
killer feature for the simulation/robotics audience.
[docs/12-future-research-topics.md](../12-future-research-topics.md) topic 5
identifies the real blocker: two independently-produced `.chronicle` files
share no object identity/address (different process runs — Phase 4's
generation-counted handles are single-run scoped by construction), so
diffing needs a semantic alignment key instead, and leaves open whether
this is a Query API extension or a separate tool.

## Decision
A separate CLI subcommand, `chronicle-cli diff-runs <file-a> <file-b>`
(`tools/cli/diff_runs.cpp`), not a Query API extension — the existing
`chronicle::diff()` family operates on one process's live `tracked<T>`
handles, which is exactly the identity two independent runs don't share;
extending it there would mean smuggling cross-process concerns into the
in-process API `chronicle.hpp` promises stability for
([ADR 0018](0018-v1-api-stability-commitment.md)). A CLI tool operating on
two loaded `.chronicle` files, like every other `chronicle-cli` subcommand,
fits the existing shape.

Streams are aligned by **name** (`player.health`, `player.positions`) — the
semantic key docs/12 asks for, and already the natural "role" identifier
this project uses everywhere else (EnTT adapter, PMR adapter, the
interactive viewer's object-grouping). Streams present in only one run are
reported as such, not silently skipped.

For aligned scalar streams, events are compared **by ordinal position**
within each run's own history (the Nth recorded value), not by version — a
per-`Stream<T>`-instance counter with no meaning across two separate
processes, the same reason [ADR 0019](0019-hybrid-logical-clock.md)'s HLC
is explicitly single-`Session`-scoped and can't help here either. First
divergence and total divergent-position count are reported.

For aligned `IndexedOp`/`KeyedOp` streams, only **final replayed state** is
compared, reusing the existing `replay_indexed`/`replay_keyed` fold logic
(moved from `main.cpp` into `tools/cli/replay.hpp` so `cmd_diff` and
`diff_runs.cpp` share one implementation instead of two). Full op-by-op
cross-run alignment was deliberately not attempted: the same final state is
reachable via different op sequences (a different insert/erase order that
ends up equivalent), so an op-ordinal diff would report spurious
differences for two runs that actually agree — an honest scope limit, not
an oversight.

Exit code follows the standard Unix `diff` convention: 0 for no
differences, 1 for differences found, matching a scriptable/CI usage
pattern.

### Verification performed
Built two real, separately-produced `.chronicle` files from a standalone
program (a `tracked<int>` scalar and a `tracked_vector<int>`, one run's
values perturbed by a seed argument to simulate a diverging scenario) and
ran `chronicle-cli diff-runs` against them: correctly reported the scalar
stream's first divergence at ordinal position 2 (80 vs 81) and the vector's
final-state divergence at index 2 (3 vs 4), exit code 1. Re-ran against two
identically-seeded runs: reported no differences, exit code 0. Full
`chronicle-core-tests` suite unaffected by the `replay_indexed`/
`replay_keyed` extraction (still 264/264 checks, 58 tests — those functions
have no test coverage of their own yet, exercised only via `cmd_diff` and
now `diff_runs.cpp`).

## Consequences
- Positive: real "diff two runs" capability for the audience
  docs/02-competitive-gap-analysis.md named this feature for, without
  touching the stability-committed in-process API surface at all.
- Positive: `replay_indexed`/`replay_keyed` now have exactly one
  implementation instead of two that could silently drift apart.
- Negative: scalar cross-run alignment by ordinal position is a real
  assumption — it's only meaningful when both runs' scenario actually
  produces the same *number and order* of writes to a given field for the
  same reason; a scenario with branching/conditional writes could
  misalign, reported honestly as a limitation, not solved here.
- Negative: container streams only get final-state comparison, not a full
  cross-run structural diff — a real, scoped-down answer to the "is this a
  Query API extension or a different tool" question docs/12 left open:
  it's a different tool, and a narrower one than full history alignment.
