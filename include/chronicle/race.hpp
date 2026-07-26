#pragma once

#include <cstdint>

#include "chronicle/event.hpp"
#include "chronicle/hlc.hpp"

// docs/12-future-research-topics.md topic 1's "concrete, real follow-up"
// from the v2.0 deterministic-replay research spike: candidate (c) (best-
// effort ordering + flag apparent races) is ADR 0003's permanent model, and
// this is the cheap primitive that spike found the HLC (ADR 0019) already
// makes possible -- not a new mechanism, the same HlcTimestamp comparison
// snapshot_at_hlc() already does.
//
// Deliberately NOT a claim about whether a race actually happened: that
// question is only answerable with real synchronization information this
// project's source-level instrumentation model does not have (docs/04's
// documented blind spot). This answers a narrower, honestly-scoped
// question instead: "were these two events close enough in time, on
// different threads, that this project's best-effort ordering cannot tell
// you which happened first" -- true positives include real races, but also
// any two genuinely-ordered events that simply landed within the window
// (e.g. a mutex-protected critical section spanning >window_us). Callers
// (docs/adr/0016's interactive viewer) should present this as "may have
// raced," never "did race."

namespace chronicle {

// window_us == 0 (the default): only events whose HLC shares the exact same
// physical microsecond tick count as "possibly racing" -- the narrowest,
// least-false-positive-prone window, since two same-thread events can never
// land here (each thread's own tick() call serializes through the same
// Session-wide atomic, so two events from one thread are always
// separated by at least a logical-counter step or a physical tick, and the
// thread_id check below already excludes them anyway). A wider window is a
// caller-chosen trade: more sensitivity to interleavings separated by more
// wall-clock time, at the cost of more false positives from genuinely-
// ordered-but-nearby events.
template <typename T, typename U>
[[nodiscard]] bool possible_race(HistoryRecord<T> const& a, HistoryRecord<U> const& b,
                                  std::uint64_t window_us = 0) noexcept {
    if (!is_known(a.hlc) || !is_known(b.hlc)) {
        // Session::Config::causal_clock was off for (at least) one side --
        // this project has no ordering information to compare at all, so
        // "possibly racing" would be a fabricated answer, not a
        // conservative one. See docs/09-performance.md/ADR 0019 for why
        // causal_clock defaults off (a real, measured per-event cost).
        return false;
    }
    if (a.thread_id == b.thread_id) {
        // Same producing thread => strict program order between them
        // (ADR 0003's per-stream/per-thread guarantee), never a race
        // regardless of how close their HLCs land.
        return false;
    }
    std::uint64_t const diff = a.hlc.physical_us > b.hlc.physical_us
                                    ? a.hlc.physical_us - b.hlc.physical_us
                                    : b.hlc.physical_us - a.hlc.physical_us;
    return diff <= window_us;
}

} // namespace chronicle
