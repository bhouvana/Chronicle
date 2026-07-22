# Repository Structure & Project Standards

## Repository layout

```
chronicle/
├── README.md
├── LICENSE                      # Apache-2.0 or MIT — decide before v0.1 public release
├── CMakeLists.txt               # top-level, add_subdirectory per component
├── CMakePresets.json
├── conanfile.py                 # Conan recipe (parallel vcpkg port maintained too)
├── vcpkg.json
├── .github/
│   └── workflows/
│       ├── ci.yml               # build+test matrix: {gcc,clang,msvc} x {linux,macos,windows}
│       ├── bench.yml            # runs chronicle-bench, fails on regression vs. baseline
│       └── docs.yml             # builds & publishes docs site
├── include/chronicle/           # chronicle-core public headers (header-only for v0.1-v0.2:
│                                 # tracked<T>, tracked_vector<T>/tracked_map<K,V> included --
│                                 # see ADR 0006, dependency-free STL container tracking lives
│                                 # here, not under adapters/, because it has nothing to opt out of
│   └── io/                      # opt-in on-disk format: wire.hpp, format.hpp, session_writer.hpp,
│                                 # loaded_session.hpp -- plain function templates, not virtual
│                                 # dispatch on StreamBase (ADR 0008); not pulled in by chronicle.hpp
├── src/                         # chronicle-core implementation (non-header-only parts)
├── adapters/                    # reserved for adapters with a real external dependency
│   ├── entt/                    # chronicle-adapter-entt (v1.0)
│   └── allocator/               # chronicle-adapter-allocator (v2.0)
├── tools/
│   ├── cli/                     # chronicle-cli (v0.2, shipped -- list/history/diff; ADR 0005, ADR 0008)
│   ├── codegen/                 # Clang-based registration generator (v0.5+)
│   └── viewer/                  # browser viewer static assets + serve backend (still v1.0)
├── bench/                       # chronicle-bench microbenchmark suite (results tracked in bench/RESULTS.md)
├── tests/
│   ├── unit/
│   ├── integration/
│   └── conformance/             # cross-platform behavioral parity tests
├── examples/
│   ├── minimal/                 # scalar tracked<T>
│   ├── container/               # tracked_vector<T>
│   ├── map/                     # tracked_map<K,V>
│   ├── export/                  # writes demo.chronicle for chronicle-cli to read
│   ├── game-loop/
│   └── entt-integration/
├── docs/                        # this document set + generated API reference
│   ├── adr/                     # Architecture Decision Records
│   └── rfc/                     # Request for Comments (larger design proposals)
└── third_party/                 # vendored/pinned deps not available via package manager
```

## Build & packaging

- **CMake** as the canonical build system (target-based, `CMAKE_CXX_STANDARD 23`),
  with each component (`chronicle-core`, `chronicle-instrumentation`,
  `chronicle-adapter-*`, `chronicle-cli`, ...) as its own CMake target with an
  explicit `target_link_libraries` graph mirroring the dependency rules in
  [05-architecture.md](05-architecture.md) — a target linking something outside its
  allowed dependency set should be a build error, not just a convention.
- **vcpkg port + Conan recipe** maintained in parallel from v0.2 onward, once the
  API is stable enough that packaging churn doesn't outpace it.
- **CI matrix**: gcc (latest 2 major versions) + clang (latest 2) on Linux, clang on
  macOS, MSVC on Windows — the minimum needed to back the "all (target)" platform
  claim in Phase 2's positioning matrix with evidence, not assertion.

## Testing standards

- Unit tests per component, colocated under `tests/unit/<component>/`.
- **Conformance tests**: behavior that must be identical across compilers/platforms
  (e.g. "history() returns events in causal order") run on every CI matrix leg —
  catches ABI/UB-adjacent divergence early (directly defends against the Phase 4
  risks around UB and compiler-specific behavior).
- Benchmarks are tests too: `chronicle-bench` results are asserted against a
  committed baseline (`bench/baseline.json`) with a tolerance band; CI fails on
  regression beyond tolerance (operationalizes Phase 9's "measurement discipline").

## Documentation standards

- Every public API symbol has a doc comment covering: cost (Big-O and/or measured
  ns, per Phase 9), thread-safety, and failure mode — not just "what it does."
- Architecture docs (this `docs/` tree) are living documents, versioned with the
  code; a PR that changes a documented architectural boundary (Phase 5) must update
  the relevant doc in the same PR.
- ADRs (`docs/adr/`) record *why* a significant decision was made, using the
  standard "Context / Decision / Consequences" format, one file per decision,
  numbered sequentially, never edited after merge (superseded by a new ADR instead)
  — this is what lets future contributors understand why, e.g., the recording model
  uses per-stream causal ordering instead of a global clock, without re-litigating it.
- RFCs (`docs/rfc/`) are used for proposals larger than one ADR's scope (e.g. a new
  adapter category, a breaking API change) and require a comment/review period before
  acceptance, mirroring the Boost/LLVM review culture referenced throughout this
  document set.

## Contribution & governance baseline

- `CONTRIBUTING.md` covering: coding style (clang-format config committed), the
  ADR/RFC process above, and the CI/benchmark gate requirements for merge.
- License: permissive (MIT or Apache-2.0) — required for the target audiences
  (game studios, finance) who routinely reject copyleft dependencies; final choice
  is a decision for an ADR, not silently assumed.
