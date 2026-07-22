# ADR 0005: `chronicle-cli` Moves from v0.1 to v0.2 (Requires the On-Disk Session Format)

## Status
Accepted

## Context
[10-roadmap.md](../10-roadmap.md) originally scoped a standalone `chronicle-cli`
(`history`, `diff` subcommands) into v0.1, on the theory that it was a thin,
independently buildable consumer of the Query API.
[05-architecture.md](../05-architecture.md) and
[08-visualization.md](../08-visualization.md) both establish, as a hard
architectural rule, that visualization/CLI tools are **separate processes**
that consume "the Query API / session files" — never linked into the
instrumented binary.

While building v0.1's actual implementation
([RFC 0001](../rfc/0001-core-recording-and-instrumentation-model.md),
[ADR 0004](0004-mutex-staging-deque-for-v01.md)), it became clear this rule has
a consequence the original roadmap missed: a `chronicle-cli` binary is, by
definition, a *different process* from the one that recorded the session. For
it to have anything to query, the session must already have been serialized
to something that process can read — i.e. the on-disk `.chronicle` format.
That format was (correctly) scoped to v0.2
([10-roadmap.md](../10-roadmap.md)'s original v0.2 entry), not v0.1. v0.1 as
originally scoped therefore asked for a standalone CLI with no possible data
source — an unshippable combination, not merely an inconvenient one.

## Decision
`chronicle-cli` moves to v0.2, alongside the on-disk format it depends on.
v0.1's "standalone value" claim is corrected to rest on what v0.1 actually
delivers today: a library embedded directly in a program
(`examples/minimal/main.cpp` demonstrates this), queried in-process via
`history()`/`snapshot()`/`diff()` — not a separate scriptable tool.

## Consequences
- Positive: the roadmap now matches an architectural constraint the project
  already committed to (Phase 5/8's process-separation rule) instead of
  silently contradicting it.
- Positive: demonstrates the "architecture docs are living documents" rule
  from [11-repository-structure-and-standards.md](../11-repository-structure-and-standards.md)
  actually working — a real dependency-ordering mistake, caught during
  implementation, corrected in the docs rather than papered over or ignored.
- Negative: v0.1's "standalone value" pitch is weaker without a CLI — it's a
  library-only milestone. Judged acceptable: the example program and unit
  tests still make v0.1 independently demonstrable and useful, just to a
  developer embedding Chronicle in their own code rather than to someone
  wanting an external inspection tool.
- Follow-on: no other roadmap milestone was checked for the same class of
  mistake (a tool scoped before its data dependency exists) as part of this
  ADR — worth a deliberate pass before v0.5/v1.0 implementation kicks off,
  rather than discovering another one mid-build.
