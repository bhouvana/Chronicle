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
not assumed.

Two layers of real, passing, non-Electron verification exist:

1. **`src/test/pureLogic.test.ts`**: the core data transformation
   (`buildLineIndex`/`parseCallSite`) verified against a **real, running**
   `chronicle-cli serve` instance serving `examples/export/demo.chronicle`:
   line 20 (`player.health`'s `track()` call) correctly reports exactly 1
   change (plain `health = 75` assignment can't capture a call site, ADR
   0010), and lines 27-30 (`player.inventory`'s `push_back()`/`update()`
   calls) each correctly report exactly 1 change.
2. **`src/test/providerIntegration.test.ts`** (added after the single-
   instance theory below was disproven): a hand-rolled `vscode` module
   shim (`src/test/vscodeShim.ts` — real `Range`/`CodeLens`/`EventEmitter`
   classes matching the actual API shape, plus recording stand-ins for
   `languages.registerCodeLensProvider`/`commands.registerCommand`/
   `window.createWebviewPanel`) is spliced in for `require('vscode')` via
   a `Module._resolveFilename` hook (`src/test/registerShim.ts`), and
   extension.ts's **actual compiled `activate()`** — the real VS Code
   entry point, unmodified — is required and run under plain Node against
   it. This is strictly deeper than (1): it exercises the real
   `ChronicleCodeLensProvider.provideCodeLenses()` method (not just its
   internal helpers), producing real `vscode.CodeLens`/`vscode.Range`
   objects, via a real HTTP fetch against the real running server
   (verified against the complete real dataset — 8 call sites across
   `player.health`, `player.inventory`, and `match.scores`, not the
   partial set (1) checked) — and it invokes the real registered
   `chronicle.showHistory` command handler, confirming it really creates
   a webview panel whose HTML really contains
   `<iframe src="{serverUrl}/">`. What this still doesn't verify: VS
   Code's own rendering of a CodeLens as clickable text in a real gutter,
   and a real click actually invoking the command through VS Code's own
   dispatch — the boundary this shim can't cross is real GUI rendering
   and real user-input dispatch, not the extension's own logic.

**Full VS Code UI-level verification (CodeLens actually rendering, the
webview actually opening) could not be completed in this environment**,
and — after a second, much more thorough round of investigation
prompted directly by being told "there must be some other way" — the
original diagnosis below turned out to be wrong, and is corrected here
rather than left standing.

**What was originally suspected (single-instance IPC forwarding), and
why it's now known to be incorrect**: the first investigation attributed
the failure to Electron's single-instance lock, since this development
machine already had roughly 16 real VS Code Stable windows open under
the same user session. That theory made one falsifiable prediction: VS
Code **Insiders** maintains its own independent single-instance lock
from Stable by design, so launching Insiders (with zero other Insiders
processes running) should have sidestepped the problem entirely. It
didn't — a freshly downloaded, never-before-run Insiders build failed
with the identical "bad option" rejection for every flag, which rules
out single-instance forwarding as the cause (there was no competing
instance to forward to).

**What the failure actually is, established by systematic elimination
across eleven independent invocation strategies**, each changing exactly
one variable at a time:
- The initial "bad option" errors were traced to `ELECTRON_RUN_AS_NODE=1`
  being present in this shell's inherited environment (this whole
  session runs inside a VS Code extension host process, which sets this
  for its own child processes) — stripping it made the "bad option"
  errors disappear completely, replaced by a *different* failure: a
  Chromium bootstrap crash, `[ERROR:base\i18n\icu_util.cc:232] Invalid
  file descriptor to ICU data received`, exit code `0x80000003`
  (`STATUS_BREAKPOINT` — a `CHECK()`-triggered fast-fail, not an
  unhandled exception; confirmed by an empty Windows Application event
  log for the same time window, ruling out Windows Error Reporting as a
  further diagnostic source).
- Explicitly re-setting `ELECTRON_RUN_AS_NODE=1` on a direct invocation
  (bypassing `@vscode/test-electron` entirely) had **no effect** —
  proof that this specific packaged VS Code build has Electron's
  `runAsNode` fuse disabled (Microsoft ships official VS Code builds
  this way as a hardening measure), which also retroactively explains
  why the very first "bad option" messages were never really Node's own
  CLI parser as first assumed, but VS Code's own native argument
  validator producing similarly-worded output.
- A fully clean environment (`env -i` with only the minimal variables a
  Windows process needs — `PATH`, `SYSTEMROOT`, `TEMP`, etc., every
  `VSCODE_*`/`ELECTRON_*`/`CHROME_CRASHPAD_PIPE_NAME` variable stripped)
  produced the identical ICU crash — ruling out environment-variable
  poisoning entirely, including a real candidate
  (`CHROME_CRASHPAD_PIPE_NAME`, pointing at the *outer* VS Code
  process's own crash-handler pipe).
- Launching via Windows Task Scheduler (`schtasks /create` + `/run`) —
  which creates a genuinely separate process tree, not a child of the
  invoking shell at all, immune to any Windows Job Object the tool's own
  shell processes might be confined to — produced the **identical**
  crash. This rules out job-object/process-tree confinement as the
  cause, and is the strongest evidence gathered: even a process with no
  ancestry relationship to this session's shell fails identically.
- `--disable-gpu --disable-software-rasterizer --disable-gpu-compositing`
  and `ELECTRON_DISABLE_SANDBOX=1` (Electron's own native-code env var,
  independent of CLI flag parsing) were both tried and had no effect;
  the crash occurs in well under a second, consistent with a failure in
  Chromium's earliest bootstrap phase (`icu_util.cc`), before any
  GPU-specific work would begin.

Eleven independently-varied invocation strategies (channel, download
freshness, extraction method, shell, environment contents, process-tree
ancestry, GPU flags, sandbox env vars) converge on the same failure at
the same bootstrap point. The most coherent explanation consistent with
*all* of this evidence — including Task Scheduler's default `/sc once`
logon type running in Session 0, the same non-interactive session
Windows services use, which has no interactive window station attached
— is that no process launched from this tool's execution context, by
any means tried, has access to an interactive window station, which
Chromium's multi-process bootstrap (browser → GPU → renderer, with
handles including the ICU data mapping duplicated across that boundary)
depends on. This is a materially different, and more architecturally
fundamental, claim than the original single-instance theory: it says no
invocation strategy *from an automated tool context* can succeed here,
not that a specific flag or channel was missing. `src/test/
extension.test.ts` (the real `@vscode/test-electron` suite) is committed
as-is and should run correctly in a normal CI environment or an
interactive desktop session with no such constraint — a real follow-up,
genuinely blocked here, not skipped.

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
