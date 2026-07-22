# ADR 0013: Tracy Bridge via an Explicit `PlotHandle`, Not a Compile-Time Hook

## Status
Accepted

## Context
[08-visualization.md](../08-visualization.md)'s "Interop over reinvention"
section calls for a Tracy bridge: emit Chronicle mutation events as Tracy
plot data points, live, when a Tracy profiler is connected, so teams already
running Tracy see Chronicle-tracked field history inline in a tool they
already trust instead of Chronicle building a competing timeline UI
(explicit non-goal, Phase 3).

Two design constraints shaped this, both real rather than assumed:
- **Chronicle-core must stay dependency-free for consumers who don't want
  Tracy**, the same principle [ADR 0008](0008-cli-avoids-streambase-virtual-dispatch.md)
  and [tools/codegen](0012-chronicle-codegen-libtooling.md) already apply —
  most Chronicle users won't have Tracy installed, so `stream.hpp` and the
  rest of `chronicle-core` cannot `#include <tracy/Tracy.hpp>` or link
  `Tracy::TracyClient` unconditionally.
- **`Stream<T>` is a header-only template**, included and instantiated
  independently by every translation unit that touches it. A compile-time
  `#ifdef` toggling `record()`'s body between "plots" and "doesn't plot"
  would make the same `Stream<int>` instantiation have a different ABI/
  behavior depending on which TU compiled it — an ODR violation the moment
  one TU defines the macro and another doesn't, not a hypothetical concern
  given how easily a large project's build flags can differ per target.

## Decision
`Stream<T>` gains exactly one new extension point:
`set_record_hook(RecordHook hook, void* context)`, where `RecordHook` is a
plain function pointer (`void(*)(void*, T const&, std::source_location
const&)`), stored as two members defaulted to `nullptr`. `record()` checks
the hook pointer (one branch) and calls it, synchronously, with the
just-recorded value — this is the *entire* core-library change; `stream.hpp`
never includes anything Tracy-related and has no idea what the hook is for.
Runtime opt-in via a plain data member sidesteps the ODR problem a compile-
time `#ifdef` would create: every TU compiles the identical template body,
and whether a given `Stream<T>` *instance* has a hook attached is ordinary
runtime state, not a per-TU compilation difference.

`include/chronicle/tracy_bridge.hpp` — a genuinely separate, optional header
— provides `chronicle::tracy_bridge::plot(stream, name)`, returning a
move-only `PlotHandle<T>` that owns the plot's name string (Tracy retains
only the `char const*` it's given, so it must outlive every future
`record()` call, not just the `attach()` call) and detaches the hook on
destruction. This is the same explicit-handle shape as
[ADR 0011](0011-tracked-type-explicit-handle.md)'s `TrackedType`, for the
same reason: an implicit global registry keyed by anything address-based is
exactly the bug class [ADR 0009](0009-lock-free-ring-buffer.md) already
found and fixed once in this codebase; an RAII handle the caller holds
sidesteps it by construction. `static_assert(std::is_arithmetic_v<T>)`
rejects non-numeric fields at the call site — Tracy's `PlotData()` overload
set is exactly `{int64_t, float, double}`, so anything else is a
compile-time error here, not a confusing linker error inside Tracy's
headers. (A plain `int` still needed an explicit `if constexpr`
integral-vs-floating dispatch in `PlotHandle::on_record()`: `int` is
equally-ranked-convertible to both `int64_t` and `float`, so passing it to
`TracyPlot` directly is ambiguous, not implicitly resolved — caught by
actually compiling the example, not anticipated in advance.)

`examples/tracy/` is a new opt-in example (`CHRONICLE_BUILD_TRACY_EXAMPLE`,
default `OFF`), gated by `find_package(Tracy CONFIG QUIET)` with a graceful
`return()` when not found — the same pattern `tools/codegen` established for
optional, dependency-heavy targets. Unlike the codegen tool's from-source
LLVM build, Tracy has a real vcpkg port (`tracy[cli-tools]`) that ships a
proper `TracyConfig.cmake`, so `find_package(Tracy CONFIG)` works directly
against a normal vcpkg install — no from-source build or manual link line
was needed here.

### Verification performed
Scope note first: `docs/08` mentions "plot data points / zone messages" —
this pass implements only the plot-data path (arithmetic `tracked<T>`
fields). Zone messages for structural (`tracked_vector<T>`/
`tracked_map<K,V>`) or non-arithmetic mutations are a real, separate
follow-up, not attempted alongside this — scoped down deliberately, per this
project's standing practice of recording what's deferred rather than
quietly shipping a partial version of something broader.

The plot-data path was verified **headlessly and end-to-end**, not just
compiled: `examples/tracy/main.cpp` attaches a `PlotHandle` to a
`tracked<int>` field and mutates it 40 times over ~4 seconds.
`tracy-capture` (vcpkg's `tracy[cli-tools]` feature — a real, non-GUI CLI
that connects to a running Tracy client and records a `.tracy` capture
file, the same way the actual Tracy profiler GUI would) was run
concurrently and produced a genuine capture (`Frames: 2, Time span: 4.47s`,
matching the example's runtime). `tracy-csvexport -u -p` on that capture
file confirmed **exactly 40** `player_1.health` plot data points, with
values `97, 98, 95, 96, 93, ...` down to `60` — precisely matching the
example's `-3/+1` alternating mutation pattern starting from `100`, and
exactly matching the example's own printed `final health = 60`. This is the
same rigor as the HTML export's Playwright-verified browser rendering
([08-visualization.md](../08-visualization.md)'s v0.2 work) — a tool
actually consuming Chronicle's output through the real external interface,
not just source-level plausibility.

`Stream<T>::record()`'s one new branch was measured, not assumed
negligible: a controlled A/B (two Clang 21.1.6 `-O2` builds, one with a
temporary macro compiling the check back out, run interleaved) found the
with/without gap indistinguishable from this environment's own run-to-run
noise floor (a shared ~20-25ns spike hit both variants in one run,
confirming it was ambient system load, not the hook). See
`bench/RESULTS.md`'s "Tracy bridge record hook overhead" section for the
full numbers — reported as "no measurable regression at this noise floor,"
not rounded up to a false-precision number.

The full unit suite (208/208 checks, 38 tests) was re-run after the
`Stream<T>` change and passes unchanged.

## Consequences
- Positive: chronicle-core stays exactly as dependency-free as before for
  every consumer who doesn't `#include <chronicle/tracy_bridge.hpp>` — one
  function-pointer member and one branch is the entire footprint.
- Positive: the bridge is verified against a real external tool consuming
  real captured data, not just "the code calls `TracyPlot`" — the same
  standard this project already held its HTML export to.
- Negative: only arithmetic scalar fields can be plotted this pass;
  structural container mutations and non-arithmetic fields get no bridge
  coverage yet (the "zone messages" half of docs/08's original framing) —
  explicitly deferred, not silently dropped.
- Negative: `set_record_hook()` is not synchronized against concurrent
  `record()` calls on other threads — attach/detach must happen outside any
  window of concurrent producer activity on that stream, the same lifetime
  discipline as constructing/destroying the `Stream` itself, not a new
  hazard. Documented in `stream.hpp` directly rather than solved with
  atomics for a scenario (concurrent attach/detach mid-recording) this
  feature has no real use case for yet.
- Follow-on: a `TracyMessage`-based path for structural/non-arithmetic
  mutations, if a real need for it shows up.
