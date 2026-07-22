#pragma once

#include <atomic>
#include <cstdint>

// docs/adr/0019-hybrid-logical-clock.md (docs/10-roadmap.md's v2.0 item):
// an opt-in, tie-free ordinal comparable *across different streams* --
// something per-stream version counters structurally cannot give
// (Stream<T>::current_version() means nothing compared against a
// different Stream<U>'s). docs/06-recording-model.md and
// docs/adr/0003-causal-not-global-ordering.md both describe this as
// filling an already-"reserved" wire-format field; verified directly (not
// assumed) that no such field exists anywhere in event.hpp or the on-disk
// format -- a real, previously-undiscovered gap between the original
// design sketch and what got built, same category as the columnar-format
// and `[[chronicle::track]]`-attribute gaps found earlier in this project.
// This is a genuinely new field, format v3 -> v4, not filling in a
// pre-reserved slot.
//
// Packed into a single lock-free std::atomic<std::uint64_t> (44 bits
// physical microseconds since the owning Session's start() + 20 bits
// logical tie-breaker) rather than a wider atomic struct: this project's
// hottest path already went through the trouble of eliminating a mutex
// once (docs/adr/0009-lock-free-ring-buffer.md) and isn't reintroducing
// one, or a 128-bit CAS whose lock-free-ness would need re-verifying per
// platform, for an opt-in feature most sessions won't enable.

namespace chronicle {

struct HlcTimestamp {
    std::uint64_t physical_us = 0; // microseconds since the owning Session's start()
    std::uint32_t logical = 0;     // tie-breaker within the same physical_us tick

    [[nodiscard]] friend bool operator==(HlcTimestamp const&, HlcTimestamp const&) = default;
    [[nodiscard]] friend bool operator<(HlcTimestamp const& a, HlcTimestamp const& b) noexcept {
        return a.physical_us != b.physical_us ? a.physical_us < b.physical_us : a.logical < b.logical;
    }
    [[nodiscard]] friend bool operator<=(HlcTimestamp const& a, HlcTimestamp const& b) noexcept {
        return a < b || a == b;
    }
};

// A default-constructed (all-zero) HlcTimestamp means "not computed for
// this event" (the owning Session::Config::causal_clock was false) -- same
// "zero is a real sentinel, not a fabricated value" convention as
// event.hpp's call_site/is_known().
[[nodiscard]] inline bool is_known(HlcTimestamp const& hlc) noexcept {
    return hlc.physical_us != 0 || hlc.logical != 0;
}

class HybridLogicalClock {
public:
    // physical_now_us: elapsed microseconds since the owning Session's
    // start() -- the caller (Session::next_hlc_tick()) owns computing this
    // from steady_clock so this class stays clock-agnostic and unit-testable
    // without real time passing.
    [[nodiscard]] HlcTimestamp tick(std::uint64_t physical_now_us) noexcept {
        std::uint64_t old_packed = packed_.load(std::memory_order_relaxed);
        std::uint64_t new_packed = 0;
        do {
            auto const old_physical = old_packed >> kLogicalBits;
            auto const old_logical = old_packed & kLogicalMask;
            std::uint64_t new_physical;
            std::uint64_t new_logical;
            if (physical_now_us > old_physical) {
                new_physical = physical_now_us;
                new_logical = 0;
            } else if (old_logical < kLogicalMask) {
                // Same (or an out-of-order-observed earlier) physical tick:
                // advance the logical counter, guaranteeing strict
                // monotonicity even when steady_clock reads the same value
                // twice in a row -- exactly the real clock-tie failure mode
                // docs/adr/0007-timestamp-ties-under-optimization.md found
                // for the plain-timestamp path, solved structurally here
                // instead of needing a separate exact-version escape hatch.
                new_physical = old_physical;
                new_logical = old_logical + 1;
            } else {
                // kLogicalMask (2^20 - 1) same-tick collisions is not
                // reachable by any measured cost in this codebase (tens of
                // ns/op, not sub-picosecond) -- but if it ever were, borrow
                // a physical tick rather than silently overflowing into it.
                new_physical = old_physical + 1;
                new_logical = 0;
            }
            new_packed = (new_physical << kLogicalBits) | new_logical;
        } while (!packed_.compare_exchange_weak(old_packed, new_packed, std::memory_order_relaxed));
        return HlcTimestamp{new_packed >> kLogicalBits, static_cast<std::uint32_t>(new_packed & kLogicalMask)};
    }

private:
    static constexpr int kLogicalBits = 20; // ~1M same-microsecond ties before borrowing a tick
    static constexpr std::uint64_t kLogicalMask = (1ull << kLogicalBits) - 1;
    std::atomic<std::uint64_t> packed_{0};
};

} // namespace chronicle
