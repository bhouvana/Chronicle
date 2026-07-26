# ADR 0042: `--json` Output for `objects`/`doctor`/`narrate`

## Status
Accepted

## Context
A proposed "AI becomes trivial" framing — an LLM answering questions from
a Chronicle session instead of reading source — was evaluated alongside a
full query language and separated out as the actually-cheap, concretely
buildable part: an LLM (or any tool) doesn't need a query grammar, it
needs **structured, machine-parseable output** instead of a human-
formatted text report. This is a fraction of the cost of
[ADR 0041](0041-doctor-and-rules.md)'s deferred query-language proposal
and delivers the same practical unlock.

## Decision
A trailing `--json` flag, stripped once in `main()` before the existing
dispatch chain (so every subcommand's exact-arg-count matching stays
unchanged), switches `objects`, `doctor`, and `narrate` to a second
renderer over the **exact same already-computed data** — no new analysis,
just a different serialization of what the human-readable path already
produces. `tools/cli/json_util.hpp/.cpp` provides one shared
`json_escape()` — unlike `html_export.cpp`/`perfetto_export.cpp`'s
independently-duplicated copies (deliberately self-contained exporters
targeting genuinely different external systems), these three commands are
one concept (structured tooling/AI output) with no reason to diverge, so
one shared helper, not three.

### Verification performed
All three JSON outputs validated with a real, independent parser
(`python -m json.tool`), not eyeballed. `narrate --json` against the real
persistence demo correctly serialized a full 5-frame call chain, a real
derivation explanation, and correctly-escaped Windows backslash paths in
`source_file` fields — the exact class of bug hand-rolled JSON emission
most often gets wrong, caught by validating with a real parser rather than
assuming string concatenation was correct. `doctor --json` and
`objects --json` similarly validated with real content matching their
human-readable counterparts exactly. Full suite unaffected (this touches
only `tools/cli`).

## Consequences
- Positive: the concrete, low-cost version of "AI-readable Chronicle" —
  buildable in an afternoon, not a multi-month query-engine investment.
- Positive: zero new analysis code — pure re-serialization, so the JSON
  and text outputs can never disagree about facts, only about formatting.
- Negative: no formal JSON schema published yet — a real, cheap follow-on
  if external tooling starts depending on this shape, not attempted
  speculatively ahead of a real consumer.
- Negative: only three commands got `--json`; the rest of the CLI
  (`list`/`history`/`diff`/etc.) still only emits text — a natural,
  bounded extension if a real need for it shows up.
