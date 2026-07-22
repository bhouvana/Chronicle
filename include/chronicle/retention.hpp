#pragma once

#include <cstddef>

// Retention & overflow policy types.
// See docs/06-recording-model.md ("Retention policy is explicit, per-stream...")
// and docs/rfc/0001-core-recording-and-instrumentation-model.md.

namespace chronicle {

// What happens when a stream's pending-event staging area is full.
// v0.1 implements this against a mutex-protected deque (see stream.hpp);
// the lock-free per-thread ring buffer design in docs/06 remains the
// target hot-path implementation once chronicle-bench establishes a
// baseline to optimize against (docs/09-performance.md).
enum class OverflowPolicy {
    DropOldest,  // evict the oldest pending event to make room (default)
    DropNewest,  // discard the incoming event, keep what's already staged
    Block        // never drop; caller blocks until space is available
};

// A stream's history retention policy. Two v0.1 modes per RFC 0001's scope:
// RingWindow keeps only the most recent N events (self-bounding memory),
// Unbounded keeps everything for the life of the session.
class RetentionPolicy {
public:
    [[nodiscard]] static RetentionPolicy ring_window(std::size_t max_events) noexcept {
        return RetentionPolicy(false, max_events);
    }
    [[nodiscard]] static RetentionPolicy unbounded() noexcept {
        return RetentionPolicy(true, 0);
    }

    [[nodiscard]] bool is_unbounded() const noexcept { return unbounded_; }
    [[nodiscard]] std::size_t max_events() const noexcept { return max_events_; }

private:
    RetentionPolicy(bool unbounded, std::size_t max_events) noexcept
        : unbounded_(unbounded), max_events_(max_events) {}

    bool unbounded_;
    std::size_t max_events_;
};

} // namespace chronicle
