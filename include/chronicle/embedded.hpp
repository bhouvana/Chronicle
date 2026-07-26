#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// docs/12-future-research-topics.md topic 7, docs/adr/0027-embedded-tier.md.
// Phase 2 concluded "mostly no" for constrained embedded targets, but
// flagged higher-end automotive/industrial ECUs as plausible "if there's a
// tiny, allocation-free tier." This is that tier -- a genuinely separate,
// additive module, not a modification of chronicle::Session/Stream<T>
// (which fundamentally use std::vector/std::unique_ptr/std::mutex --
// swapping those out from underneath the primary game/sim/finance
// audience's existing API would be the "compromising the core model's
// generality" this topic explicitly warns against). Nothing in this header
// is reachable from #include <chronicle/chronicle.hpp>; it's an opt-in
// module a caller reaches for by name, same shape as the Tracy bridge or
// the PMR adapter.
//
// Deliberately NOT thread-safe and NOT a drop-in tracked<T> replacement:
// no atomics, no mutex, no ring-buffer-per-thread machinery. The audience
// this targets (a single-core or cooperatively-scheduled ECU task) doesn't
// need Stream<T>'s concurrency story, and paying for it here would
// reintroduce exactly the kind of hidden cost docs/09-performance.md's
// zero-cost philosophy exists to avoid. If a caller's embedded target
// genuinely has concurrent producers, external synchronization is their
// responsibility -- this class does not attempt to provide it.

namespace chronicle::embedded {

// Fixed-capacity, stack/static-storage-only circular history for one
// arithmetic-or-trivially-copyable scalar field. No heap allocation at any
// point in this class's lifetime -- verified directly (not assumed) by
// tests/unit/embedded_test.cpp overriding operator new/delete globally and
// asserting the allocation counter never moves.
template <typename T, std::size_t Capacity>
class TrackedScalar {
    static_assert(Capacity > 0, "TrackedScalar needs at least one slot of history");

public:
    constexpr TrackedScalar() = default;
    explicit constexpr TrackedScalar(T initial) { record(initial); }

    TrackedScalar& operator=(T value) {
        record(value);
        return *this;
    }

    constexpr operator T const&() const noexcept { return current_; }
    [[nodiscard]] constexpr T const& get() const noexcept { return current_; }

    // Total number of values ever recorded, including ones already evicted
    // from `buffer_` -- the fixed-capacity counterpart to Stream<T>::current_version(),
    // except here it also doubles as "how many of buffer_'s Capacity slots
    // are actually populated" via size() below.
    [[nodiscard]] constexpr std::uint64_t total_recorded() const noexcept { return count_; }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return count_ < Capacity ? static_cast<std::size_t>(count_) : Capacity;
    }

    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return Capacity; }

    // Oldest-to-newest indexing over whatever history is still retained
    // (older events are silently overwritten once total_recorded() exceeds
    // Capacity -- the fixed-footprint trade this tier exists to make; there
    // is no on-disk/unbounded escape hatch here, unlike RetentionPolicy).
    [[nodiscard]] constexpr T const& operator[](std::size_t index) const noexcept {
        // Once the buffer has wrapped (count_ > Capacity), head_ already
        // points at the next slot to be overwritten -- which is exactly
        // the oldest surviving value's slot. Before it wraps, the oldest
        // value is simply at index 0 (nothing has been evicted yet).
        std::size_t const oldest_slot = count_ > Capacity ? head_ : 0;
        return buffer_[(oldest_slot + index) % Capacity];
    }

private:
    void record(T value) {
        current_ = value;
        buffer_[head_] = value;
        head_ = (head_ + 1) % Capacity;
        ++count_;
    }

    T current_{};
    std::array<T, Capacity> buffer_{};
    std::size_t head_ = 0;
    std::uint64_t count_ = 0;
};

} // namespace chronicle::embedded
