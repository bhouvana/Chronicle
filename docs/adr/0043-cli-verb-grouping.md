# ADR 0043: `inspect`/`compare`/`analyze` as Additive CLI Verb Prefixes

## Status
Accepted

## Context
By this point `chronicle-cli` had accumulated 14+ flat subcommands
(`list`, `history`, `diff`, `diff-runs`, `merge`, `objects`,
`object-history`, `object-snapshot`, `query {most-changed,threads,thread}`,
`program-history`, `program-snapshot`, `narrate`, `doctor`, `export`,
`serve`). A proposed regrouping under clearer verbs
(`chronicle inspect`/`compare`/`analyze`/`doctor`) was evaluated for real
discoverability value.

## Decision
`inspect`, `compare`, and `analyze` are **purely additive prefixes**,
stripped in `main()` before the existing dispatch chain runs —
`chronicle-cli inspect objects <file>` reaches the exact same code path as
`chronicle-cli objects <file>`. [ADR 0018](0018-v1-api-stability-commitment.md)
explicitly does not cover CLI syntax, so nothing here was a compatibility
obligation — but breaking every existing invocation (scripts, muscle
memory, this project's own examples) for a cosmetic reorganization would
have been a real, avoidable UX regression with no corresponding benefit.
Every flat command keeps working completely unchanged, verified directly
(grouped and flat forms produce byte-identical output side by side), not
assumed from the diff.

**Deliberately not an enforced category**: typing `chronicle-cli inspect diff ...`
(a "compare"-flavored command under the "inspect" prefix) still dispatches
correctly — the groups are a documented, printed convention
(`print_usage()`), not a second command grammar with its own validation
that could drift out of sync with the real dispatch table.

### Verification performed
`inspect objects`, `analyze doctor --json`, and `compare merge` each
tested directly against real files and confirmed to dispatch identically
to their flat equivalents (`objects`, `doctor --json`, `merge`) — same
output, same exit codes. Full suite unaffected (this touches only
`tools/cli/main.cpp`'s argument preprocessing).

## Consequences
- Positive: real discoverability improvement (`chronicle-cli inspect
  <tab>` groups related commands) at essentially zero implementation or
  maintenance cost — a handful of lines of prefix-stripping, no new
  dispatch table to keep in sync.
- Positive: zero breaking changes — every flat invocation from earlier
  ADRs' own verification transcripts in this document set still works
  exactly as documented there.
- Negative: the grouping is unenforced, so it can't catch a user's
  mistaken assumption about which group a command belongs to — accepted
  as the right trade for not building and maintaining a second, stricter
  command grammar for marginal additional guidance.
