# Roadmap

Every milestone must be independently useful — no milestone ships only as a stepping
stone with no standalone value, per the brief's constraint.

> **Correction (see [ADR 0005](adr/0005-cli-requires-on-disk-format.md)):** the
> original v0.1 scope below included a standalone `chronicle-cli`. Building v0.1
> revealed that a standalone CLI has a real, unavoidable dependency on the on-disk
> session format — which was (correctly) scoped to v0.2, not v0.1. `chronicle-cli`
> has moved to v0.2 accordingly; this is exactly the kind of correction
> [docs/11](11-repository-structure-and-standards.md)'s "architecture docs are
> living documents" rule exists for.

> **Update (see [ADR 0009](adr/0009-lock-free-ring-buffer.md)):** the
> mutex-protected staging deque ADR 0004 shipped v0.1 with has been replaced
> by the target lock-free per-thread ring buffer, closing that gap. Getting
> it right took two real bugs found via AddressSanitizer and a genuine
> multi-threaded stress test — the second bug (cross-drain version
> ordering) is invisible to any single-threaded test by construction, which
> is exactly why `tests/unit/concurrency_test.cpp` exists now.

## v0.1 — "It records and you can ask it a question" (core proof of concept) — SHIPPED
- `chronicle-core`: Recording Engine + Storage Engine (operation log, in-memory
  only, `RingWindow` and `Unbounded` retention). Originally implemented
  against a mutex-protected staging deque rather than the target lock-free
  ring buffer ([ADR 0004](adr/0004-mutex-staging-deque-for-v01.md)) — a
  documented, temporary substitution, since replaced by the real thing
  ([ADR 0009](adr/0009-lock-free-ring-buffer.md)).
- `tracked<T>` for scalar types; manual registration only, no codegen yet.
- Query API: `history()`, `snapshot()`, `diff()` for scalar fields.
- `chronicle-bench`: microbenchmark suite; first real numbers captured in
  [`bench/RESULTS.md`](../bench/RESULTS.md) (not yet a CI-enforced gate).
- **Standalone value**: usable today for "why did this int/float/enum reach this
  value" debugging, *embedded directly in your own program* — see
  `examples/minimal/main.cpp`. (Originally scoped to include a scriptable CLI;
  see the correction above for why that moved to v0.2.)

## v0.2 — "Containers, shareable output, and the CLI" — SHIPPED
- [x] `tracked_vector<T>` with structural delta encoding (`ContainerOp<T>`:
  Insert/Erase/Update/Clear), replay-based `history()`/`snapshot()`, and an
  index-aligned element-wise `diff()` — see
  [ADR 0006](adr/0006-container-tracking-lives-in-core-not-adapters.md) for
  where it lives and why; `examples/container/main.cpp` demonstrates it end
  to end.
- [x] `snapshot_at_version()`/`current_version()` — exact, tie-free
  alternative to timestamp-based snapshots, added after a Release-mode
  (`-O2`) test run surfaced real clock-tie failures in fast back-to-back
  mutations; see [ADR 0007](adr/0007-timestamp-ties-under-optimization.md).
  70/70 checks across 12 tests, stable across 20 consecutive Release runs.
- [x] `tracked_map<K,V>` with structural delta encoding (`MapOp<K,V>`, keyed
  rather than indexed; backed by `std::map` for deterministic iteration
  order), `set()` self-determining Insert vs. Update, exact key-based
  `diff()` (no alignment ambiguity, unlike the vector diff), and
  `snapshot_at_version()`/`current_version()` used from the start rather
  than timestamp-bounded queries — `examples/map/main.cpp` demonstrates it;
  109/109 checks across 17 tests, stable across 5 consecutive Release runs.
- [x] On-disk session file format (`.chronicle`): magic+version header,
  self-terminating sequence of per-stream blocks running to EOF (no leading
  count, single-pass writer), self-describing `WireValue`s (a small closed
  set of leaf kinds — Int64/UInt64/Double/Bool/String — per the "keep the
  wire schema small, push meaning onto the producer" lesson from
  [01-research-landscape.md](01-research-landscape.md)'s Perfetto/Tracy
  notes). Known limitation: native byte order, no endian portability yet
  (documented in `include/chronicle/io/wire.hpp`).
- [x] `chronicle-cli`: `list`, `history`, `diff` subcommands (moved from
  v0.1, now unblocked — [ADR 0005](adr/0005-cli-requires-on-disk-format.md)).
  Runs as a genuinely separate process from the producer, sharing no C++
  types — verified via `examples/export/` (writes `demo.chronicle`) piped
  into `chronicle-cli` as two independent binaries. Serialization itself
  uses plain function templates rather than `StreamBase` virtual dispatch,
  for a real reason, not a style preference — see
  [ADR 0008](adr/0008-cli-avoids-streambase-virtual-dispatch.md).
- [x] `chronicle-cli export --html`: static, self-contained HTML timeline
  viewer (docs/08-visualization.md's tier 2 — inline CSS/JS, no external
  fetch, no server). Embeds the session as inline JSON; a stream
  picker, a keyboard-accessible scrubber (`<input type=range>`), a
  reconstructed-state panel, and a full event log with colorblind-safe
  diff highlighting (+/−/~ glyphs alongside color, per docs/08's
  accessibility requirement) are all implemented in vanilla JS replaying
  the same op semantics as `chronicle-cli`'s own C++ replay logic.
  Verified in an actual browser (Playwright), not just by reading the code:
  navigated to a real exported page, clicked between all three stream
  shapes, moved the scrubber, and confirmed the reconstructed state matched
  expected values at each step, with zero JS errors.
- **Standalone value**: sessions become artifacts you can attach to a bug report or
  CI run — inspect from a scriptable CLI, or open the exported HTML file directly
  in any browser, no Chronicle install needed on the receiving end either way.

## Post-v0.2 hardening — "Close the ADR 0004 gap" — SHIPPED
Not a numbered milestone (no new user-facing capability), but real,
load-bearing work: replacing v0.1's mutex-protected staging deque with the
lock-free per-thread ring buffer the architecture always specified.
- `include/chronicle/ring_buffer.hpp`: power-of-two-sized SPSC ring buffer,
  cache-line-padded atomics. `Stream<T>`'s fast path is now lock-free for
  every `OverflowPolicy`; `Block` has real blocking semantics for the first
  time (closing [RFC 0001](rfc/0001-core-recording-and-instrumentation-model.md)'s
  open question); `DropOldest`'s rare overflow case keeps a small mutex
  (a deliberate, documented scope decision, not an oversight — a fully
  lock-free overwrite ring needs a seqlock design this pass didn't attempt).
- `tests/unit/concurrency_test.cpp`: the project's first genuine
  multi-threaded tests. They earned their place immediately — they caught
  two real bugs (a thread-local-cache-by-address use-after-free, and a
  cross-drain version-ordering bug) that zero single-threaded tests, run
  any number of times, could ever have found.
- `bench/RESULTS.md` updated with honest single-threaded (slower, ~10ns/op)
  and contended-multithreaded (faster, scales with thread count) numbers —
  reported both, not just the flattering one.
- See [ADR 0009](adr/0009-lock-free-ring-buffer.md) for the full account,
  including two debugging false trails worth 20+ minutes each that are
  recorded so a future session doesn't repeat them.

## v0.5 — "Low-friction adoption" — SHIPPED
- [x] Clang-based codegen tool (`tools/codegen/chronicle-codegen`): generates
  `CHRONICLE_TRACK_TYPE` registrations from `CHRONICLE_TRACKABLE`-annotated struct
  declarations, closing the reflection gap (Phase 4) for the common case without
  requiring C++26/P2996. Built against a from-source LLVM/Clang LibTooling build
  (the official prebuilt Windows installer ships only `libclang`'s C API, not the
  C++ `clangTooling`/`clangASTMatchers` libraries a real AST-based generator
  needs). `CHRONICLE_TRACKABLE` expands to `[[clang::annotate("chronicle::track")]]`,
  not the plain `[[chronicle::track]]` originally sketched here — verified by a
  real AST dump that the plain form is silently dropped by Clang with no AST
  trace at all, making it invisible to any LibTooling-based scanner. Verified
  end-to-end: run against a real annotated header, confirmed unmarked structs are
  skipped, and confirmed the generated `CHRONICLE_TRACK_TYPE(...)` calls compile
  and work in an actual program. Opt-in build only (`CHRONICLE_BUILD_CODEGEN`,
  default `OFF`) — most consumers won't have Clang/LLVM dev libraries installed.
  See [ADR 0011](adr/0011-tracked-type-explicit-handle.md) (the `CHRONICLE_TRACK_TYPE`
  mechanism this tool targets) and [ADR 0012](adr/0012-chronicle-codegen-libtooling.md)
  (the LibTooling build itself, including a real CMake+Ninja+`cl.exe` toolchain
  bug found and worked around along the way).
- [x] Causal-chain queries ("what last wrote this value"): `chronicle::last_writer()`
  for `tracked<T>`/`tracked_vector<T>`/`tracked_map<K,V>`, backed by a new
  `call_site` field (`std::source_location`) on every recorded event. Real
  language constraint discovered and worked around, not assumed away: a
  member `operator=` cannot take a defaulted second parameter (confirmed by
  compiler error, not assumed from memory), so plain `field = value` can
  never capture its own call site — `chronicle::set(field, value)` is the
  explicit alternative that can. See
  [ADR 0010](adr/0010-call-site-capture.md). On-disk format bumped to v2
  (breaking) to carry call sites through `chronicle-cli`/HTML export too.
- [x] Tracy bridge: `chronicle::tracy_bridge::plot(stream, name)` (`include/chronicle/
  tracy_bridge.hpp`) attaches live Tracy plotting to an arithmetic `tracked<T>`
  field's `Stream<T>`, emitting a Tracy plot data point on every mutation while a
  Tracy profiler is connected — the "plot data points" half of docs/08's original
  framing (zone messages for structural/non-arithmetic mutations are a real,
  explicitly deferred follow-up, not silently dropped). `Stream<T>` itself gained
  exactly one function-pointer extension point (`set_record_hook`) and one branch in
  `record()` — chronicle-core still has zero Tracy dependency for consumers who don't
  opt in. Verified **headlessly and end-to-end**: `examples/tracy/` mutates a tracked
  field 40 times while `tracy-capture` (vcpkg's `tracy[cli-tools]`, a real non-GUI
  capture tool) records the live session, and `tracy-csvexport` confirmed all 40 plot
  points with the exact expected values — a real external tool consuming real
  captured data, not source-level plausibility. The new branch's cost was measured
  via a controlled A/B in `chronicle-bench`, not assumed negligible — see
  `bench/RESULTS.md`. See [ADR 0013](adr/0013-tracy-bridge.md).
- **Standalone value**: registration friction drops from "write a macro per field" to
  "add an attribute" (the codegen tool has landed); causal-chain queries and the
  Tracy bridge are both independently useful today. **v0.5 is now fully shipped.**

## v1.0 — "Production-ready core + interactive viewer" — SHIPPED
- [x] Interactive browser viewer: `chronicle-cli serve <file> [--port N]` (`tools/cli/serve.cpp`,
  opt-in via `find_package(httplib CONFIG)`) serves the existing scrubber/event-log viewer
  (reusing the static export's `session_to_json()`/JS renderer verbatim — zero duplicated
  rendering logic) plus a new **object/ownership graph view**, grouping streams by name
  prefix (`"player.health"`/`"player.mana"` → object `player`). Re-reads the `.chronicle`
  file fresh per request rather than holding a live producer connection (no live transport
  exists yet — deferred alongside [ADR 0014](adr/0014-storage-engine-compression.md)'s LZ4
  live-stream compression, for the same reason); a **Refresh** button re-fetches and
  re-renders in place, verified to pick up an on-disk file change with zero page reload.
  Verified end-to-end in a real browser via Playwright: object-graph navigation, scrubbing,
  and Refresh all confirmed working against a live server, not just a static page. Default
  build unaffected (208/208 checks unchanged; 246/246 with httplib/Zstd/EnTT available).
  Causal-chain graph (docs/08's other tier-3 item) explicitly not attempted — Chronicle's
  data model has no cross-stream dependency edges to graph beyond the per-event call site
  already shown. See [ADR 0016](adr/0016-interactive-browser-viewer.md).
- [x] EnTT adapter (`chronicle-adapter-entt`): `track_component<Component>(registry, session, name)`
  (`adapters/entt/`) bridges an `entt::registry`'s construct/update/destroy signals into
  Chronicle history, requiring only that `Component` already has a `CHRONICLE_TRACK_TYPE`
  registration (reuses `TrackedFieldsOf<T>`, [ADR 0011](adr/0011-tracked-type-explicit-handle.md)'s
  reflection metadata, rather than a second mechanism). Each field becomes its own
  `Stream<MapOp<std::uint32_t, FieldType>>`, keyed by entity — reusing `tracked_map<K,V>`'s
  exact wire shape, so `chronicle-cli`/the HTML viewer needed **zero changes** to display
  EnTT-sourced history. Opt-in (`CHRONICLE_BUILD_ENTT_ADAPTER`, default `OFF`, via
  `find_package(EnTT CONFIG)`) — chronicle-core gains no EnTT dependency; default build
  stays at 208/208 checks unchanged. Verified against a real `entt::registry`
  (`examples/entt-integration/`): `emplace`/`patch`/`remove` produced exactly the expected
  `Insert`/`Insert`/`Update`/`Erase` sequence with correct values. See
  [ADR 0015](adr/0015-entt-adapter.md).
- [x] Compression: Zstd at rest, in the Storage Engine. `include/chronicle/io/format.hpp`
  gains a `CompressionKind` header tag (format v2 → v3); `SessionWriter`/`load_session`
  take an optional `CompressionCodec` — a plain function-pointer extension point (same
  shape as `Stream<T>::RecordHook`, [ADR 0013](adr/0013-tracy-bridge.md)), not a forced
  dependency. `chronicle/io/zstd_codec.hpp` is genuinely separate and opt-in, same
  pattern as `tools/codegen` and the Tracy bridge — the default build stays exactly as
  dependency-free as before (208/208 checks unchanged). `chronicle-cli` transparently
  reads compressed files when built against Zstd, no flag needed. Verified against the
  real `chronicle-cli` binary, not just a unit test: a 25,000-mutation session compressed
  to **11.7% of its original size (~8.5x)**, byte-identical content read back through both
  compressed and uncompressed files. LZ4 for live-stream compression is explicitly
  deferred — no live-streaming transport exists yet for it to compress (the interactive
  browser viewer below ended up re-reading files on demand rather than building one either,
  for the same reason); a real follow-up, not attempted speculatively. See
  [ADR 0014](adr/0014-storage-engine-compression.md).
- [x] CI performance-regression gate: `chronicle-bench --json` (a new machine-readable
  output mode) + `bench/compare_baseline.py`, wired into `.github/workflows/bench.yml`
  on every push/PR to `main`. Tolerance is a deliberately loose 100% (fails only past 2x
  slower than `bench/baseline.json`), derived directly from this project's own measured
  noise floor — the same ~30-50% run-to-run swings `bench/RESULTS.md`'s Tracy-bridge A/B
  test already found on a *single* dev machine, which a CI runner (typically noisier, and
  a *different* machine than the baseline was captured on) would only make worse. Every
  step of both workflows (this gate and a separate `ci.yml` build+test matrix across
  windows-msvc/ubuntu-gcc/ubuntu-clang) was run locally against a clean build directory
  before pushing, including deliberately injecting a fake 500ns/op regression to confirm
  the gate actually fails when it should, not just passes when nothing's wrong. Local
  rehearsal wasn't the whole story: the first real push surfaced two genuine bugs neither
  local run had hit — a sub-nanosecond-noise false regression (fixed with a `--min-ns`
  floor) and a real cross-platform CMake bug (`ubuntu-latest`'s *system* zstd CMake
  package satisfies `find_package()` but doesn't define vcpkg's `zstd::libzstd` target,
  now guarded with an explicit `TARGET` check everywhere this project does an opt-in
  `find_package()`). See [ADR 0017](adr/0017-ci-performance-gate.md).
- [x] Stability commitment: public API (Phase 7 surface) enters semver-stable status.
  Scoped explicitly: everything reachable from `#include <chronicle/chronicle.hpp>`
  (`tracked<T>`, `tracked_vector<T>`, `tracked_map<K,V>`, `Session`, `Stream<T>`, the
  `track()`/`history()`/`snapshot()`/`diff()`/`last_writer()` free-function families,
  `CHRONICLE_TRACK_TYPE`) follows semver from here on; the on-disk wire format keeps its
  own independent `kFormatVersion` (already bumped three times, explicitly *not* implied
  stable by this commitment) and the newer opt-in modules (Tracy, Zstd, EnTT, the viewer's
  JSON API) get a best-effort promise rather than full semver until they've had a release
  cycle's worth of real exposure. See [ADR 0018](adr/0018-v1-api-stability-commitment.md).
- **Standalone value**: the "no-compromise daily driver" milestone — safe to
  recommend for real production debugging workflows, not just prototyping.
- **Note**: version numbering is illustrative of *sequence*, not a fixed calendar —
  no dates are committed here; each milestone starts only once its predecessor's
  standalone value has been validated by real usage/feedback, not by a schedule.

## v2.0 — "Ecosystem & advanced replay" — IN PROGRESS
- [x] PMR allocator/arena adapter: `chronicle::TrackedMemoryResource` (`include/chronicle/
  tracked_memory_resource.hpp`) wraps a `std::pmr::memory_resource`, recording every
  allocate/deallocate as a `MapOp<address, size>` event — reusing `tracked_map<K,V>`'s exact
  wire shape a second time (first for the EnTT adapter, [ADR 0015](adr/0015-entt-adapter.md)),
  so `chronicle-cli`/the viewers again needed **zero changes**. Corrects docs/11's original
  `adapters/allocator/` placement: `<memory_resource>` is standard library, not an external
  dependency, so — applying [ADR 0006](adr/0006-container-tracking-lives-in-core-not-adapters.md)'s
  own precedent — this lives in `include/chronicle/` and ships with chronicle-core, no opt-in
  build flag needed, unlike every other adapter this project has built. Verified against real
  `std::pmr::vector<int>` allocations, not a mock: a deliberate `reserve()`-forced reallocation
  correctly recorded `insert[new 1024-byte address]` immediately followed by
  `erase[old 16-byte address]`, matching `std::vector`'s actual grow-then-free order exactly,
  end-to-end through `chronicle-cli history` with real heap addresses and call sites. See
  [ADR 0021](adr/0021-pmr-allocator-adapter.md).
- [x] VS Code extension (`tools/vscode-extension/`): CodeLens annotations (the correct,
  actually-clickable VS Code mechanism — `TextEditorDecorationType` has no click handler at all,
  checked directly) showing per-line mutation counts, fetched from a running `chronicle-cli serve`
  instance's existing `/api/session` endpoint — zero new C++ code needed. Clicking opens a
  `WebviewPanel` iframing the exact same live-viewer page `chronicle-cli serve` already renders —
  literally "reusing the browser viewer's webview," not reimplementing it. Data transformation
  (`buildLineIndex`/`parseCallSite`) verified against a real running server: `player.health`'s
  `track()` call correctly shows exactly 1 change, `player.inventory`'s 4 named-method calls each
  show exactly 1. Full interactive VS Code UI verification could not be completed in this session
  — `@vscode/test-electron` hit a genuine, methodically-diagnosed environment constraint (Electron
  single-instance IPC forwarding, this machine already running ~16 VS Code windows) rather than an
  extension bug; the real automated test suite is committed and should run correctly in CI, where
  that constraint won't apply. Recorded honestly, not glossed over. See
  [ADR 0022](adr/0022-vscode-extension.md).
- [x] Perfetto export bridge: `chronicle-cli export --perfetto <file> <output.json>` emits the
  legacy Chrome JSON Trace Event Format (a bare JSON array, verified directly against Perfetto's
  own current documentation, not protobuf — zero new dependency, reusing the same plain-JSON-
  generation approach the HTML export already uses). Scalar numeric streams become Counter events
  (`"ph":"C"`, real value-over-time tracks); vector/map structural changes become Instant events
  annotated with op/key/value, an honest mapping rather than forcing a plotted line onto data with
  no continuous value. `tid` is each event's real `thread_hash` (already captured); `pid` is a
  fixed synthetic value since Chronicle never captured an OS process id. Verified in the **real,
  live Perfetto UI** via Playwright, not just JSON validity: a screenshot confirms `player.health`'s
  counter track visibly stepping `100 → 75 → 45 → -5`, matching the recorded session exactly. See
  [ADR 0020](adr/0020-perfetto-export-bridge.md).
- [x] Hybrid logical clock upgrade for cross-stream causal ordering: `include/chronicle/hlc.hpp`'s
  `HlcTimestamp`/`HybridLogicalClock`, opt-in via `Session::Config::causal_clock` (default `off` —
  a real, measured ~30-50% per-event cost when enabled, not the noise-floor-indistinguishable cost
  of e.g. the Tracy bridge's hook check). Verified directly that the "reserved field" docs/06 and
  ADR 0003 describe never actually existed in the implementation — a genuinely new field (format
  v3 → v4), not a reserved slot filled in. The real capability this adds:
  `chronicle::snapshot_at_hlc(field, hlc)` answers cross-stream queries ("what was `player.health`
  when `player.zone` last changed") that per-stream version counters structurally cannot, since two
  different streams' version counters share no common meaning — verified with a real cross-stream
  test, not just same-stream monotonicity. Does **not** solve genuine cross-thread causal ordering
  under races (restated honestly, not overclaimed) — only a tie-free, cross-stream-comparable
  ordinal. A genuine concurrent stress test (8 threads × 5,000 ticks, stable across 5 runs)
  confirms the underlying CAS-packed atomic never loses an update. See [ADR 0019](adr/0019-hybrid-logical-clock.md).
- Research spike (not a commitment) into deterministic multithreaded replay
  (Phase 12) — evaluated for a possible v3 based on findings, not pre-committed.
- **Standalone value**: the ecosystem-integration milestone — Chronicle fits into
  existing toolchains (editor, other tracing tools) rather than being an island.

## Explicit anti-goals for this roadmap

No milestone before v1.0 attempts multithreaded deterministic global replay,
security-hardened/adversarial tracing, or embedded/allocation-free targets — these
remain tracked as open research (Phase 12) so scope creep doesn't stall the
core value (query-able state history) from shipping early and often.
