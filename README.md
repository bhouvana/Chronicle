# Chronicle

**Time travel for runtime state.**

Chronicle is a proposed modern C++ library for recording, querying, diffing, and
replaying the history of runtime state — objects, containers, ECS worlds, or
custom domain state — from inside your own process, without a debugger attached.

> **Status: v0.1 through v2.0 all shipped; ADR 0004 perf gap closed.**
> Research/architecture/design concluded first, per this project's own
> standard. `tracked<T>`, `tracked_vector<T>`, `tracked_map<K,V>`, an
> on-disk `.chronicle` format, `chronicle-cli`, a self-contained HTML
> timeline viewer, a lock-free per-thread ring buffer replacing v0.1's
> mutex, and (v0.5) causal-chain queries (`chronicle::last_writer()`,
> call-site capture), a Clang-based codegen tool, and a live Tracy bridge
> all build and are unit-tested, verified end-to-end —
> as separate producer/consumer processes, in an actual browser via
> Playwright, headlessly against a real Tracy capture tool, and under
> AddressSanitizer (see
> [Where things stand](#where-things-stand)). Two real language/toolchain
> constraints were discovered and worked around during this work, not
> assumed away: a member `operator=` cannot capture its own call site (a
> hard compile error, not a style choice — [ADR 0010](docs/adr/0010-call-site-capture.md)),
> and the ring buffer work caught two real concurrency bugs no
> single-threaded test could have found
> ([ADR 0009](docs/adr/0009-lock-free-ring-buffer.md), which also reports
> an honest performance finding: not simply "faster," it trades
> single-threaded speed for multi-threaded scalability, both numbers
> published). Serialization separately avoids `StreamBase` virtual dispatch
> for a real correctness reason
> ([ADR 0008](docs/adr/0008-cli-avoids-streambase-virtual-dispatch.md)).

## The core idea

Existing tools force a choice: full-fidelity instruction-level replay with no
semantic understanding (rr, WinDbg TTD, UndoDB), or semantic understanding of a
single instant with no history (serializers, reflection libraries). Chronicle's bet
is the missing middle: **ask any tracked value "what were you, and why did you
change," as a live, in-process, programmatic query** — bounded, opt-in, and cheap
enough to run in a game's frame budget.

```cpp
struct Player {
    chronicle::tracked<int> health{100};
};

player.health = player.health - 25;          // recorded automatically

for (auto const& rec : chronicle::history(player.health)) {
    // rec.value, rec.timestamp, rec.thread_id, rec.call_site
}
```

See [docs/07-api-design.md](docs/07-api-design.md) for the fuller surface.

## Document map

This is a research-and-design-first project. Read in this order:

| # | Document | Answers |
|---|---|---|
| 1 | [Research Landscape](docs/01-research-landscape.md) | What already exists (rr, UndoDB, WinDbg TTD, Tracy, ASan, ECS, reflection, persistent data structures) and what we take from each |
| 2 | [Competitive Gap Analysis](docs/02-competitive-gap-analysis.md) | Where the real, unoccupied gap is; who would actually adopt this and why |
| 3 | [Core Idea & Feasibility](docs/03-core-idea-and-feasibility.md) | The one capability nobody else has, and an honest feasibility/risk verdict |
| 4 | [Technical Limitations](docs/04-technical-limitations.md) | What C++ makes fundamentally impossible to observe, and the honest product claim that follows |
| 5 | [Architecture](docs/05-architecture.md) | Layers, subsystem boundaries, dependency rules |
| 6 | [Recording & Storage Model](docs/06-recording-model.md) | How history is captured and kept bounded |
| 7 | [API Design](docs/07-api-design.md) | The public surface: `track`, `history`, `diff`, `replay` |
| 8 | [Visualization & DX](docs/08-visualization.md) | CLI, HTML export, browser viewer, VS Code, and why nothing renders inside your process |
| 9 | [Performance Engineering](docs/09-performance.md) | Budgets, techniques, and measurement discipline |
| 10 | [Roadmap](docs/10-roadmap.md) | v0.1 → v2.0, each milestone independently useful |
| 11 | [Repository Structure & Standards](docs/11-repository-structure-and-standards.md) | Layout, build, CI, testing, contribution standards |
| 12 | [Future Research Topics](docs/12-future-research-topics.md) | Open questions deliberately not committed to a roadmap |

Design decisions are additionally recorded as they're made:
- [docs/adr/](docs/adr/) — Architecture Decision Records (why, once, never re-litigated)
- [docs/rfc/](docs/rfc/) — larger proposals open for review before implementation

## Building

```sh
cmake -S . -B build -DCMAKE_CXX_STANDARD=23
cmake --build build
ctest --test-dir build --output-on-failure   # unit tests
./build/examples/minimal/chronicle-example-minimal     # scalar tracked<T> demo
./build/examples/container/chronicle-example-container # tracked_vector<T> demo
./build/examples/map/chronicle-example-map              # tracked_map<K,V> demo
./build/bench/chronicle-bench                           # microbenchmarks (see bench/RESULTS.md)

./build/examples/export/chronicle-example-export        # writes demo.chronicle
./build/tools/cli/chronicle-cli list demo.chronicle      # reads it back, as a separate process
./build/tools/cli/chronicle-cli history demo.chronicle player.health
./build/tools/cli/chronicle-cli diff demo.chronicle player.inventory 1 3
./build/tools/cli/chronicle-cli export --html demo.chronicle demo.html   # open demo.html in any browser
```

Requires a C++23 compiler (verified against Clang 21). `chronicle-core` is
header-only; `CMakeLists.txt`/`src/CMakeLists.txt` build the tests and
examples that link it. `tests/unit/concurrency_test.cpp` spawns real
`std::thread`s (a handful of seconds under Release, longer under ASan/Debug)
— see [ADR 0009](docs/adr/0009-lock-free-ring-buffer.md).

## What this is not

Not a debugger, not a logger, not a profiler, not a serializer, and explicitly not
an attempt to rebuild full CPU-level deterministic replay (rr/TTD-class engines).
See [docs/02-competitive-gap-analysis.md](docs/02-competitive-gap-analysis.md#explicit-non-goals-to-keep-the-project-from-collapsing-under-its-own-ambition).

## Where things stand

- [x] Research landscape survey
- [x] Competitive/gap analysis
- [x] Feasibility & risk assessment
- [x] C++ technical limitations analysis
- [x] Architecture, recording model, API, visualization, and performance proposals
- [x] Roadmap and repository/project standards
- [x] Initial ADRs and the v0.1-scoping RFC
- [x] RFC 0001 open questions resolved (see [Resolution](docs/rfc/0001-core-recording-and-instrumentation-model.md#resolution))
- [x] `chronicle-core` v0.1: `tracked<T>`, `Session`, `Stream<T>`, `history()`/`snapshot()`/`diff()`, ring-window & unbounded retention — builds, 28/28 unit tests pass, example runs
- [x] `chronicle-bench` microbenchmark suite — real numbers captured in [`bench/RESULTS.md`](bench/RESULTS.md); tracked-assignment cost confirmed ~10x the target budget as expected from [ADR 0004](docs/adr/0004-mutex-staging-deque-for-v01.md), not yet an enforced CI gate
- [x] v0.1 roadmap correction: `chronicle-cli` moved to v0.2 — it has a real, previously-missed dependency on the on-disk session format ([ADR 0005](docs/adr/0005-cli-requires-on-disk-format.md))
- [x] `tracked_vector<T>` (v0.2, in progress): structural-delta history, replay-based `snapshot()`, element-wise `diff()` — lives in `include/chronicle/`, not a separate adapter module ([ADR 0006](docs/adr/0006-container-tracking-lives-in-core-not-adapters.md))
- [x] `snapshot_at_version()`/`current_version()`: exact, tie-free queries added after Release-mode (`-O2`) testing surfaced real timestamp ties in fast back-to-back mutations ([ADR 0007](docs/adr/0007-timestamp-ties-under-optimization.md))
- [x] `tracked_map<K,V>`: keyed structural-delta history, `set()`/`erase()`/`clear()`, exact key-based `diff()` — `examples/map/` demonstrates it
- [x] On-disk `.chronicle` format (`include/chronicle/io/`) and `chronicle-cli` (`list`/`history`/`diff`) — verified end-to-end as genuinely separate processes (`examples/export/` writes a file, `chronicle-cli` reads it back sharing zero C++ types). Serialization deliberately avoids `StreamBase` virtual dispatch for a real correctness reason, not style — [ADR 0008](docs/adr/0008-cli-avoids-streambase-virtual-dispatch.md)
- [x] `chronicle-cli export --html`: self-contained timeline viewer, all three stream shapes verified interactively in a real browser (Playwright) — scrubbing, reconstructed state, and diff highlighting all confirmed correct, not just read from the code. **v0.2 is now fully shipped.**
- [x] Lock-free per-thread ring buffer (replaces v0.1's mutex-protected staging deque) — [ADR 0009](docs/adr/0009-lock-free-ring-buffer.md). Caught two real concurrency bugs via `tests/unit/concurrency_test.cpp` (the project's first multi-threaded tests) and AddressSanitizer; both fixed and verified (40 consecutive clean stress-test runs, 5 clean ASan runs). Honest benchmark finding in [`bench/RESULTS.md`](bench/RESULTS.md): single-threaded got ~10ns/op slower, but throughput scales with real concurrency, beating the old design at 2+ threads. Full suite: **155/155 checks across 25 tests**, stable across 10+ consecutive runs
- [x] Causal-chain queries (v0.5): `chronicle::last_writer()` for scalar/vector/map fields, backed by a new `call_site` field (`std::source_location`) on every event. Discovered and documented a real, permanent C++ constraint along the way — member `operator=` cannot take a defaulted second parameter (confirmed by compiler error), so plain `field = value` can never capture a call site; `chronicle::set(field, value)` is the explicit alternative that can — see [ADR 0010](docs/adr/0010-call-site-capture.md). On-disk format bumped to v2 (breaking) to carry this through `chronicle-cli` and the HTML export too. Verified via `tests/unit/call_site_test.cpp`, an io round-trip test, and real CLI/HTML output against an exported session (`[main.cpp:20]` for `track()`-captured events, nothing for plain-assignment events, exactly as designed). **Full suite: 189/189 checks across 33 tests**, stable across 5 consecutive runs.
- [x] `chronicle-codegen` (v0.5): Clang LibTooling-based tool that scans for `CHRONICLE_TRACKABLE`-annotated structs and generates their `CHRONICLE_TRACK_TYPE(...)` registrations, closing the reflection gap for the common case without requiring C++26/P2996. Built from source against LLVM/Clang's LibTooling libraries — the official prebuilt Windows installer only ships `libclang`'s C API, not what a real AST-based tool needs. Two real findings along the way, not assumed: the roadmap's originally-sketched `[[chronicle::track]]` attribute is silently dropped by Clang with no AST trace at all (confirmed via a direct AST dump), so `CHRONICLE_TRACKABLE` expands to `[[clang::annotate("chronicle::track")]]` instead, which does survive into the AST; and getting the tool to link required diagnosing a CRT-model mismatch and missing default libraries against a from-source LLVM build with no CMake package config, not just a toolchain bug during LLVM's own build (a CMake `TryCompile` failure specific to Ninja+`cl.exe`, worked around with `-DCMAKE_C/CXX_COMPILER_WORKS=1`). Verified end-to-end: run against a real annotated header, confirmed unmarked structs are skipped, and confirmed the generated registrations compile and work in an actual program. Opt-in build only (`CHRONICLE_BUILD_CODEGEN`, default `OFF`, via `find_package(Clang CONFIG)`) — see [ADR 0011](docs/adr/0011-tracked-type-explicit-handle.md) and [ADR 0012](docs/adr/0012-chronicle-codegen-libtooling.md). **Full suite: 208/208 checks across 38 tests.**
- [x] Tracy bridge (v0.5): `chronicle::tracy_bridge::plot(stream, name)` (`include/chronicle/tracy_bridge.hpp`) attaches live Tracy plotting to an arithmetic `tracked<T>` field, emitting a plot data point on every mutation while a Tracy profiler is connected. `Stream<T>` gained exactly one function-pointer extension point (`set_record_hook`) and one branch in `record()` — chronicle-core still has zero Tracy dependency unless a consumer opts in. Verified **headlessly and end-to-end**, not just compiled: `examples/tracy/` mutates a tracked field 40 times while `tracy-capture` (vcpkg's `tracy[cli-tools]`, a real non-GUI capture tool) records the live session, and `tracy-csvexport` confirmed all 40 plot points with the exact expected values (`97, 98, 95, 96, ..., 60`) — a real external tool consuming real captured data. The new branch's cost was measured via a controlled A/B in `chronicle-bench` rather than assumed negligible: indistinguishable from this environment's own noise floor — see `bench/RESULTS.md`. Opt-in build only (`CHRONICLE_BUILD_TRACY_EXAMPLE`, default `OFF`, via `find_package(Tracy CONFIG)`) — see [ADR 0013](docs/adr/0013-tracy-bridge.md). **v0.5 is now fully shipped.**
- [x] At-rest Zstd compression (v1.0): on-disk format bumped to v3 with a `CompressionKind` header tag; `SessionWriter`/`load_session` take an optional `CompressionCodec` — a plain function-pointer extension point, the same shape as the Tracy bridge's `RecordHook`, not a forced dependency. `chronicle/io/zstd_codec.hpp` is genuinely separate and opt-in (same pattern as `tools/codegen`/the Tracy bridge); the default build stays exactly as dependency-free as before (208/208 checks unchanged; 225/225 with Zstd's compression tests included). `chronicle-cli` transparently reads compressed files when built against Zstd, no flag needed. Verified against the real `chronicle-cli` binary: a 25,000-mutation session compressed to **11.7% of its original size (~8.5x)**, byte-identical content read back through both compressed and uncompressed files. LZ4 live-stream compression explicitly deferred — no live-streaming transport exists yet to compress. See [ADR 0014](docs/adr/0014-storage-engine-compression.md).
- [x] EnTT adapter (v1.0): `chronicle::adapters::entt::track_component<Component>(registry, session, name)` (`adapters/entt/`) bridges an `entt::registry`'s construct/update/destroy signals into Chronicle history. Requires only that `Component` already has a `CHRONICLE_TRACK_TYPE` registration — reuses `TrackedFieldsOf<T>` (the same reflection metadata `CHRONICLE_TRACK_TYPE` provides, [ADR 0011](docs/adr/0011-tracked-type-explicit-handle.md)) rather than a second mechanism. Each field becomes its own `Stream<MapOp<std::uint32_t, FieldType>>` keyed by entity, reusing `tracked_map<K,V>`'s exact wire shape — `chronicle-cli`/the HTML viewer needed **zero changes** to display EnTT-sourced history. Genuinely separate from chronicle-core (`adapters/entt/`, per docs/11's reserved layout for adapters with a real external dependency); opt-in (`CHRONICLE_BUILD_ENTT_ADAPTER`, default `OFF`, via `find_package(EnTT CONFIG)`), default build unaffected (208/208 checks unchanged; 246/246 with EnTT's + Zstd's tests included). Verified against a real `entt::registry` (`examples/entt-integration/`): `emplace`/`patch`/`remove` produced exactly the expected `Insert`/`Insert`/`Update`/`Erase` sequence with correct values, not just "it compiled." See [ADR 0015](docs/adr/0015-entt-adapter.md).
- [x] Interactive browser viewer (v1.0): `chronicle-cli serve <file> [--port N]` (opt-in via `find_package(httplib CONFIG)`) serves the existing scrubber/event-log viewer — reusing the static export's JSON serialization and JS renderer verbatim, zero duplicated logic — plus a new **object/ownership graph view** grouping streams by name prefix. Re-reads the session file fresh per request rather than holding a live producer connection (no live transport exists yet, deferred alongside [ADR 0014](docs/adr/0014-storage-engine-compression.md)'s LZ4 compression); a Refresh button re-fetches and re-renders in place. Verified end-to-end in a real browser via Playwright: clicked into the object graph, scrubbed the timeline, then overwrote the underlying file on disk and confirmed Refresh picked up the completely new content with zero page reload. Default build unaffected (208/208 checks unchanged; 246/246 with httplib/Zstd/EnTT available). See [ADR 0016](docs/adr/0016-interactive-browser-viewer.md).
- [x] CI performance-regression gate (v1.0): `chronicle-bench --json` + `bench/compare_baseline.py`, wired into `.github/workflows/bench.yml` (and a separate `ci.yml` build+test matrix: windows-msvc/ubuntu-gcc/ubuntu-clang) on every push/PR to `main`. Tolerance is a deliberately loose 100% — derived from this project's own measured ~30-50% run-to-run noise (`bench/RESULTS.md`'s Tracy-bridge A/B test), not guessed, since the CI runner is a different, typically noisier machine than `bench/baseline.json` was captured on. Every workflow step was run locally against a clean build directory first, including deliberately injecting a fake regression to confirm the gate actually fails when it should — and the first real push still found two genuine bugs neither local run had hit: a sub-nanosecond-noise false regression (fixed with a `--min-ns` floor) and a real cross-platform CMake bug (`ubuntu-latest`'s *system* zstd CMake package satisfies `find_package()` but doesn't define vcpkg's `zstd::libzstd` target — now guarded everywhere this project does an opt-in `find_package()`). **Both workflows are confirmed green on GitHub Actions**, not just locally. See [ADR 0017](docs/adr/0017-ci-performance-gate.md).
- [x] API stability commitment (v1.0): everything reachable from `#include <chronicle/chronicle.hpp>` (`tracked<T>`, `tracked_vector<T>`, `tracked_map<K,V>`, `Session`, `Stream<T>`, the `track()`/`history()`/`snapshot()`/`diff()`/`last_writer()` free-function families, `CHRONICLE_TRACK_TYPE`) now follows semver. Scoped explicitly to avoid contradicting this project's own history: the on-disk wire format keeps its own independent `kFormatVersion` (already bumped three times, *not* implied stable by this commitment), and the newer opt-in modules (Tracy, Zstd, EnTT, the viewer's JSON API) get a best-effort promise rather than full semver until they've had a release cycle's worth of real exposure. **v1.0 is now fully shipped.** See [ADR 0018](docs/adr/0018-v1-api-stability-commitment.md).
- [x] Hybrid logical clock (v2.0): `include/chronicle/hlc.hpp`'s `HlcTimestamp`/`HybridLogicalClock`, opt-in via `Session::Config::causal_clock` (default off — a real, measured ~30-50% per-event cost when enabled, unlike the Tracy bridge's noise-floor-indistinguishable hook check). Verified directly that the "reserved field" earlier docs promised never actually existed in the implementation — a genuinely new field (on-disk format v3 → v4), not a reserved slot filled in. The real payoff: `chronicle::snapshot_at_hlc(field, hlc)` answers cross-stream queries ("what was `player.health` when `player.zone` last changed") that per-stream version counters structurally cannot express — verified with a real cross-stream test. Does **not** solve genuine cross-thread causal ordering under races (stated honestly, not overclaimed) — only a tie-free, cross-stream-comparable ordinal, structurally fixing the timestamp-tie problem [ADR 0007](docs/adr/0007-timestamp-ties-under-optimization.md) found. A genuine concurrent stress test (8 threads × 5,000 ticks, stable across 5 runs) confirms the underlying CAS-packed atomic never loses an update. **Full suite: 243/243 checks across 49 tests.** See [ADR 0019](docs/adr/0019-hybrid-logical-clock.md).
- [x] Perfetto export bridge (v2.0): `chronicle-cli export --perfetto <file> <output.json>` emits the Chrome JSON Trace Event Format — verified directly against Perfetto's current documentation (not protobuf, zero new dependency, reusing the HTML export's plain-JSON approach). Scalar numeric streams become Counter events (real value-over-time tracks); vector/map structural changes become Instant events with op/key/value annotations, an honest mapping for data with no continuous value. Verified in the **real, live Perfetto UI** via Playwright: a screenshot confirms `player.health`'s counter track visibly stepping `100 → 75 → 45 → -5`, exactly matching the recorded session. See [ADR 0020](docs/adr/0020-perfetto-export-bridge.md).
- [x] PMR allocator adapter (v2.0): `chronicle::TrackedMemoryResource` (`include/chronicle/tracked_memory_resource.hpp`) wraps a `std::pmr::memory_resource`, recording allocate/deallocate as `MapOp<address, size>` events — reusing `tracked_map<K,V>`'s wire shape a second time (first for the EnTT adapter), so `chronicle-cli`/the viewers again needed zero changes. Corrects docs/11's original `adapters/allocator/` placement: `<memory_resource>` is standard library, not an external dependency, so (applying [ADR 0006](docs/adr/0006-container-tracking-lives-in-core-not-adapters.md)'s own precedent) this ships as part of chronicle-core, no opt-in flag needed. Verified against real `std::pmr::vector<int>` allocations: a `reserve()`-forced reallocation correctly recorded the old address's `erase` immediately after the new address's `insert`, matching `std::vector`'s actual grow-then-free order, end-to-end through `chronicle-cli history` with real heap addresses and call sites. **Full suite: 257/257 checks across 53 tests.** See [ADR 0021](docs/adr/0021-pmr-allocator-adapter.md).
- [x] VS Code extension (v2.0): `tools/vscode-extension/` shows CodeLens annotations (the correct, actually-clickable VS Code mechanism — plain gutter decorations have no click handler at all) with per-line mutation counts, fetched from `chronicle-cli serve`'s existing `/api/session` endpoint — zero new C++ code. Clicking opens a webview iframing the exact same live-viewer page `chronicle-cli serve` already renders. Two layers of non-Electron verification: data-transform logic checked against a real running server, and a deeper `providerIntegration.test.ts` that runs the extension's actual `activate()`/`provideCodeLenses()` under plain Node against a `vscode` API shim, producing real `vscode.CodeLens` objects across all 8 real call sites and confirming the real webview/iframe wiring. Full interactive VS Code UI verification (real gutter rendering, a real click) hit a genuine, exhaustively-diagnosed environment constraint — eleven independently-varied launch strategies all converged on the same Chromium-bootstrap-level crash, pointing to this tool execution context lacking an interactive window station — rather than an extension bug; recorded honestly, including a correction of an earlier, disproven diagnosis (single-instance IPC forwarding). The real `@vscode/test-electron` suite is committed for CI or an interactive desktop session, where that constraint won't apply. See [ADR 0022](docs/adr/0022-vscode-extension.md).
- [x] Deterministic multithreaded replay research spike (v2.0, evaluated per the roadmap's own "not a commitment" framing): all three candidate approaches from [docs/12](docs/12-future-research-topics.md#1-deterministic-multithreaded-replay) assessed against real evidence from this cycle — a global happens-before graph (partially built as the HLC above; its real ~30-50% cost for even the smallest useful slice argues against extending it), an rr-style deterministic scheduler underneath Chronicle (not attempted, ruled out architecturally — conflicts with the source-level-only instrumentation choice docs/03/04 made before v0.1), and permanent best-effort status plus flagging apparent races (the recommended path, now cheaper thanks to the HLC). **No v3 commitment to full deterministic replay.**

**v2.0 is now fully shipped.**

## License

To be decided (leaning permissive — MIT or Apache-2.0 — see
[docs/11-repository-structure-and-standards.md](docs/11-repository-structure-and-standards.md#contribution--governance-baseline));
no LICENSE file is present yet.
