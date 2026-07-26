# Chronicle

**Time travel for runtime state.**

[![CI](https://github.com/bhouvana/Chronicle/actions/workflows/ci.yml/badge.svg)](https://github.com/bhouvana/Chronicle/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)

Chronicle is a header-only C++23 library for recording, querying, diffing, and
replaying the history of runtime state — objects, containers, ECS worlds, or
custom domain state — from inside your own process, without a debugger attached.

Existing tools force a choice: full-fidelity instruction-level replay with no
semantic understanding (rr, WinDbg TTD, UndoDB), or semantic understanding of a
single instant with no history (serializers, reflection libraries). Chronicle
fills the middle: ask any tracked value **"what were you, and why did you
change,"** as a live, in-process, programmatic query — bounded, opt-in, and
cheap enough to run in a game's frame budget.

## Quick start

```cpp
#include <chronicle/chronicle.hpp>

chronicle::Session session;
chronicle::tracked<int> health{100};
chronicle::track(health, session, "player.health");

chronicle::set(health, health.get() - 25);   // recorded automatically

for (auto const& rec : chronicle::history(health)) {
    // rec.value, rec.timestamp, rec.thread_id, rec.call_site
}
```

See [docs/07-api-design.md](docs/07-api-design.md) for the full public surface.

## Installation

**CMake `FetchContent`** (works today, no extra step):

```cmake
include(FetchContent)
FetchContent_Declare(
    chronicle
    URL https://github.com/bhouvana/Chronicle/archive/refs/tags/v2.1.1.tar.gz
)
FetchContent_MakeAvailable(chronicle)

target_link_libraries(your_target PRIVATE chronicle::core)
```

**Build and install locally, then `find_package`:**

```sh
git clone https://github.com/bhouvana/Chronicle.git
cmake -S Chronicle -B build
cmake --install build --prefix /your/prefix
```

```cmake
find_package(chronicle CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE chronicle::core)
```

**vcpkg**: a port is maintained in [`packaging/vcpkg/`](packaging/vcpkg/) and
builds cleanly against the current release today via an overlay port; public
registry submission is in progress.

```sh
vcpkg install chronicle --overlay-ports=path/to/Chronicle/packaging/vcpkg
```

**Conan**: a recipe is maintained in [`packaging/conan/`](packaging/conan/);
Conan Center submission is in progress.

```sh
conan create packaging/conan --version 2.1.1
```

Requires a C++23 compiler. CI builds and tests against MSVC, GCC, and Clang on
every push.

## Features

- **Track anything**: `tracked<T>`, `tracked_vector<T>`, `tracked_map<K,V>` —
  automatic history on every mutation, zero cost when untracked.
- **Query, diff, and replay**: `history()`, `snapshot_at_version()`, `diff()`,
  causal-chain queries (`last_writer()`), cross-stream queries via a hybrid
  logical clock.
- **Object-graph aware**: group fields into objects and reconstruct one
  object's — or the whole program's — state at any point in time.
- **Explain, not just observe**: `derive()`/`explain()` for auto-recomputed
  derived state with attribution; `set_with_stacktrace()` for full call-chain
  provenance.
- **Catch problems automatically**: `possible_race()`, statistical anomaly
  detection, container growth/leak detection, and `rules::check_rule()`/
  `watch()` for runtime invariants — offline or live.
- **A real CLI** (`chronicle-cli`): diff and merge across runs and processes,
  a one-command health report (`doctor`) suitable as a CI gate, a
  self-contained HTML timeline viewer, an interactive browser view, and
  `--json` output for tooling and AI consumption.
- **Bridges to existing tools**: EnTT, PMR allocators, Tracy (live plotting),
  Perfetto trace export, and a VS Code extension.
- **Runs on constrained targets**: `chronicle::embedded::TrackedScalar` —
  fixed-capacity, zero-heap-allocation history.

```sh
# doctor: one command, tells you what's wrong
chronicle-cli doctor session.chronicle

# narrate: why is this object's state what it is right now
chronicle-cli narrate session.chronicle player 42

# same commands, grouped and machine-readable
chronicle-cli analyze doctor session.chronicle --json
```

Every feature ships with real end-to-end verification, not just unit tests —
see [docs/10-roadmap.md](docs/10-roadmap.md) and [docs/13-vision.md](docs/13-vision.md)
for the full development history and the evidence behind each one, including
what was deliberately *not* built and why (a general query language, full
deterministic replay, GPU/network bridges).

## Building from source

```sh
cmake -S . -B build -DCMAKE_CXX_STANDARD=23
cmake --build build
ctest --test-dir build --output-on-failure   # unit tests

./build/examples/minimal/chronicle-example-minimal     # scalar tracked<T> demo
./build/examples/container/chronicle-example-container # tracked_vector<T> demo
./build/examples/map/chronicle-example-map             # tracked_map<K,V> demo
./build/bench/chronicle-bench                          # microbenchmarks (see bench/RESULTS.md)

./build/examples/export/chronicle-example-export        # writes demo.chronicle
./build/tools/cli/chronicle-cli list demo.chronicle      # reads it back, as a separate process
./build/tools/cli/chronicle-cli history demo.chronicle player.health
./build/tools/cli/chronicle-cli diff demo.chronicle player.inventory 1 3
./build/tools/cli/chronicle-cli export --html demo.chronicle demo.html   # open demo.html in any browser

# object graph / narrative / health-report tooling
./build/tools/cli/chronicle-cli objects demo.chronicle
./build/tools/cli/chronicle-cli narrate demo.chronicle player 5
./build/tools/cli/chronicle-cli doctor demo.chronicle --json
```

`chronicle-core` is header-only; the root `CMakeLists.txt` additionally builds
tests, examples, benchmarks, and `chronicle-cli` when Chronicle is the
top-level project (these are skipped automatically when consumed via
`FetchContent`/`add_subdirectory`). `tests/unit/concurrency_test.cpp` spawns
real `std::thread`s and takes a few seconds under Release, longer under
ASan/Debug — see [ADR 0009](docs/adr/0009-lock-free-ring-buffer.md).

## Documentation

Design documentation, in reading order:

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
| 10 | [Roadmap](docs/10-roadmap.md) | Full development history, milestone by milestone, each independently useful |
| 11 | [Repository Structure & Standards](docs/11-repository-structure-and-standards.md) | Layout, build, CI, testing, contribution standards |
| 12 | [Future Research Topics](docs/12-future-research-topics.md) | Open questions evaluated against real evidence |
| 13 | [Vision](docs/13-vision.md) | The long-horizon framing (object graph, provenance, derived state, rules, ...) and what's deliberately capped and why |

Design decisions are recorded as they're made:
- [docs/adr/](docs/adr/) — Architecture Decision Records (why, once, never re-litigated)
- [docs/rfc/](docs/rfc/) — larger proposals open for review before implementation

## What this is not

Not a debugger, not a logger, not a profiler, not a serializer, and explicitly
not an attempt to rebuild full CPU-level deterministic replay (rr/TTD-class
engines). See
[docs/02-competitive-gap-analysis.md](docs/02-competitive-gap-analysis.md#explicit-non-goals-to-keep-the-project-from-collapsing-under-its-own-ambition).

## Contributing

See [docs/11-repository-structure-and-standards.md](docs/11-repository-structure-and-standards.md#contribution--governance-baseline)
for repository layout, testing standards, and the contribution/governance
baseline. Design changes are proposed as an ADR or RFC before implementation.

## License

[Apache License 2.0](LICENSE).
