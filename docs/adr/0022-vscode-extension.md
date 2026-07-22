# ADR 0022: VS Code Extension Uses CodeLens (Not Decorations) and an Iframed Webview

## Status
Accepted

## Context
[10-roadmap.md](../10-roadmap.md)'s v2.0 scope calls for a VS Code
extension: "inline gutter annotations ('this field changed N times in this
session, click to see history'), reusing the browser viewer's webview."
[08-visualization.md](../08-visualization.md) frames it as "a thin shell
over the same rendering code, not a separate viewer implementation."

Two things were checked directly before writing any code:
- **VS Code's `TextEditorDecorationType` has no click handler at all** —
  a real gutter/inline decoration can show text or an icon, but there is
  no API for "run this command when the user clicks this decoration."
  `CodeLens` is the actual, correct VS Code mechanism for "an annotated
  count that's clickable" — the same one VS Code's own built-in
  "N references" annotations use. The roadmap's "gutter annotations"
  phrasing is descriptive of the desired UX, not a literal API
  prescription; CodeLens delivers that UX correctly, a raw decoration
  could not.
- **What data already exists to build this from**: `chronicle-cli serve`'s
  `/api/session` endpoint ([ADR 0016](0016-interactive-browser-viewer.md))
  already returns exactly the JSON needed — every event with a known call
  site includes `"callSite":"filename:line"` (ADR 0010). Grouping by that
  field is the entire mechanism; no new C++ code, no new endpoint, no new
  wire format needed.

## Decision
`tools/vscode-extension/` (a new location — docs/11's layout sketch didn't
reserve one, since a VS Code extension wasn't anticipated to need its own
directory distinct from `tools/cli`/`tools/codegen`): a small TypeScript
extension that periodically fetches `${chronicle.serverUrl}/api/session`
(default `http://127.0.0.1:8080`, matching `chronicle-cli serve`'s own
default port), groups events by `(file, line)`, and registers a
`CodeLensProvider` for C/C++ documents. A line with recorded mutations gets
a CodeLens like `$(history) 4 changes recorded — player.health (4)`;
clicking it opens a `WebviewPanel` containing an `<iframe src="${serverUrl}/">`
— literally "reusing the browser viewer's webview," not re-implementing
its scrubber/rendering logic a third time.

Matching happens by basename only (`document.fileName`'s final path
component against `callSite`'s `filename` field) — inherited directly from
`html_export.cpp`'s own existing choice to serialize only the basename,
not a full path ("the full path is where the producing machine's build
tree happened to be, rarely useful to a reader on a different machine").
A real, honest consequence: two different files sharing a name (this
project alone has several `main.cpp`s) will show each other's annotations
if both happen to be open — an inherited limitation from ADR 0010's own
design, not a new one this extension introduces, and not attempted to be
solved here (a real fix would need call sites to carry a relative path,
a wire-format change out of scope for a visualization-layer consumer).

Fetch failures (server not running) are swallowed silently, not surfaced
as an error: CodeLenses simply stay empty until the next successful
refresh. A tool whose server is *usually* not running (most C++ editing
sessions have no `chronicle-cli serve` open) must not interrupt normal
editing over that — the same "fail quietly on the cold path, never disrupt
the hot one" posture this project applies to `Stream<T>::record()` itself,
applied here to "don't disrupt the editor."

### Verification performed, and its real limits
TypeScript compiles cleanly under `strict` mode with zero errors — real,
not assumed. The core data transformation
(`buildLineIndex`/`parseCallSite`) was verified against a **real, running**
`chronicle-cli serve` instance serving `examples/export/demo.chronicle`:
line 20 (`player.health`'s `track()` call) correctly reports exactly 1
change (plain `health = 75` assignment can't capture a call site, ADR
0010), and lines 27-30 (`player.inventory`'s `push_back()`/`update()`
calls) each correctly report exactly 1 change — the exact expected mapping
from real recorded data, not a hand-constructed fixture.

**Full VS Code UI-level verification (CodeLens actually rendering, the
webview actually opening) could not be completed in this environment**,
and this is recorded honestly rather than glossed over: `@vscode/test-
electron` (the official framework for this) failed identically across
three independent invocation strategies — a freshly-downloaded isolated
VS Code build, a manually-corrected extraction of that same build, and
the already-installed system VS Code — all three rejecting every launch
flag (including VS Code's own always-valid `--extensionDevelopmentPath`,
tested completely in isolation with no other flags) as "bad option."
This is Electron's single-instance IPC forwarding: this development
machine already has roughly 16 real VS Code windows open under the same
user session, and any new `Code.exe` launch — regardless of install path
or `--user-data-dir` — gets forwarded to one of those already-running
processes' restricted second-instance argument handler, which doesn't
understand extension-test-host flags. Confirmed methodically, not
assumed: the identical failure across three unrelated binaries rules out
a corrupted download as the cause. The fix (closing the user's existing
VS Code windows to free the singleton lock) was not taken — that's a
disruptive action affecting the user's own active work, well outside
this task's authorization. `src/test/extension.test.ts`
(the real `@vscode/test-electron` suite, asserting on
`vscode.executeCodeLensProvider`'s actual output against the real API) is
committed as-is and should run correctly in a normal CI environment with
no competing VS Code instances — a real follow-up, not attempted here to
keep this ADR's claims limited to what was actually verified in this
session.

## Consequences
- Positive: zero new C++ code — this extension is a pure consumer of
  infrastructure that already existed (`/api/session`, `callSite`), the
  third time this project has gotten a "free" feature this way (after the
  EnTT and PMR allocator adapters both reusing `MapOp`).
- Positive: "reusing the browser viewer's webview" is literal, not just
  aspirational — the webview is an iframe onto the exact same page
  `chronicle-cli serve` already renders, not a reimplementation.
- Negative: full interactive verification is an honest gap in this
  session specifically, not a claim of "verified in real VS Code" this
  ADR doesn't actually support. The real `@vscode/test-electron` suite
  exists and should be run in CI, where this environment's specific
  constraint (many pre-existing VS Code windows) won't apply.
- Negative: basename-only file matching inherits ADR 0010's existing
  limitation (same-named files in different directories collide) rather
  than solving it — a real, separate follow-up if it becomes a practical
  problem, not attempted speculatively here.
- Negative: no packaging/publishing (`.vsix`, marketplace listing) was
  attempted — this ships as source under `tools/vscode-extension/`, loaded
  via `--extensionDevelopmentPath` for now, matching this ADR's own
  verification method.
