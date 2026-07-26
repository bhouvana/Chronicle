# ADR 0028: `chronicle-cli merge` for Combining Per-Process Session Files

## Status
Accepted

## Context
[docs/12-future-research-topics.md](../12-future-research-topics.md) topic 6
notes that everything in this document set assumes a single process, and
that extending `StateStream` semantics *across* a process boundary
(network transport, clock synchronization beyond the HLC, a materially
different trust model) is substantial, separate scope — explicitly "worth
revisiting only after v2.0's ecosystem milestone validates strong
single-process adoption."

## Decision
Rather than build real network transport or cross-process clock
synchronization (the genuinely substantial scope topic 6 describes),
`chronicle-cli merge <output> <tag>:<file> [<tag>:<file> ...]`
(`tools/cli/merge.cpp`) does the smallest real thing that's still useful:
combines multiple already-captured, independently-produced `.chronicle`
files (one per process) into one file, namespacing each process's streams
by a caller-supplied tag (`server.player.health` vs `client.player.health`)
so identically-named streams from different processes don't collide.

**Explicitly does NOT establish any ordering between streams from
different input files.** Each event's `version`/`elapsed_ns`/`thread_hash`
are preserved verbatim from their source process — real data, not
fabricated — but merging doesn't change what they already meant: per
[ADR 0003](0003-causal-not-global-ordering.md), a `version` only ever
compared meaningfully within its own stream, and `elapsed_ns` is relative
to *that process's own* `Session::start()`, with no relationship to a
different process's start time at all. Synthesizing a cross-process
ordering from this would require exactly the clock synchronization work
topic 6 flags as substantial, separate scope — not attempted here. The
real, honest capability this ships is narrower: viewing/querying multiple
processes' captured histories side by side through the existing tools
(`list`/`history`/`diff`/`diff-runs`/`export`, all of which work unmodified
on a merged file, since it's ordinary format-v4 output), each process's own
internal order intact.

Implemented at the `LoadedSession`/`WireValue` level
(`chronicle::io::write_string`/`write_wire_value`/etc. from
`chronicle/io/wire.hpp` and `format.hpp`), not by replaying through a live
`chronicle::Session` — this tool, like every other `chronicle-cli`
subcommand, never has the producer's original C++ types
([ADR 0005](0005-cli-requires-on-disk-format.md)), only what
`loaded_session.hpp` already parsed.

### Verification performed
Merged two real, separately-generated per-process session files (tagged
`server`/`client`) and confirmed the output is a normal, valid v4 file:
`chronicle-cli list` shows all 4 streams correctly namespaced
(`server.player.health`, `server.player.positions`,
`client.player.health`, `client.player.positions`), and
`chronicle-cli history merged.chronicle server.player.health` reproduces
that process's exact original event sequence with correct values and
elapsed-time fields. Full `chronicle-core-tests` suite unaffected
(288/288 checks, 67 tests — this feature touches only `tools/cli`, not
`chronicle-core`).

## Consequences
- Positive: a real, useful multi-process capability — inspecting several
  processes' captured histories together — without fabricating a
  cross-process causal ordering this project has no real mechanism for.
- Positive: output is ordinary format-v4 data; no new reader logic needed
  anywhere else in the toolchain.
- Negative: not the "extend `StateStream` semantics across a process
  boundary" topic 6 originally envisioned — no live network transport, no
  real-time multi-process capture, offline/file-based only. A genuine
  live-transport version remains exactly the substantial, separate research
  area topic 6 describes, not attempted here.
- Negative: callers who don't read this ADR could misread the preserved
  `elapsed_ns` fields as cross-process-comparable when they explicitly are
  not — worth surfacing prominently in any future viewer support for
  merged files, not just this ADR.
