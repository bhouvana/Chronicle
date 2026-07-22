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

## v1.0 — "Production-ready core + interactive viewer" — IN PROGRESS
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
  the gate actually fails when it should, not just passes when nothing's wrong. See
  [ADR 0017](adr/0017-ci-performance-gate.md).
- Stability commitment: public API (Phase 7 surface) enters semver-stable status.
- **Standalone value**: the "no-compromise daily driver" milestone — safe to
  recommend for real production debugging workflows, not just prototyping.
- **Note**: version numbering is illustrative of *sequence*, not a fixed calendar —
  no dates are committed here; each milestone starts only once its predecessor's
  standalone value has been validated by real usage/feedback, not by a schedule.

## v2.0 — "Ecosystem & advanced replay"
- PMR allocator/arena adapter.
- VS Code extension.
- Perfetto export bridge.
- Hybrid logical clock upgrade for cross-stream causal ordering (Phase 6's reserved
  field), improving on best-effort timestamp correlation.
- Research spike (not a commitment) into deterministic multithreaded replay
  (Phase 12) — evaluated for a possible v3 based on findings, not pre-committed.
- **Standalone value**: the ecosystem-integration milestone — Chronicle fits into
  existing toolchains (editor, other tracing tools) rather than being an island.

## Explicit anti-goals for this roadmap

No milestone before v1.0 attempts multithreaded deterministic global replay,
security-hardened/adversarial tracing, or embedded/allocation-free targets — these
remain tracked as open research (Phase 12) so scope creep doesn't stall the
core value (query-able state history) from shipping early and often.
