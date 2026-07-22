#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <vector>

// The lock-free per-thread ring buffer docs/05-architecture.md and
// docs/06-recording-model.md specify as the Recording Engine's target
// hot-path mechanism, and that ADR 0004 deferred until a benchmark baseline
// existed to justify building it correctly. bench/RESULTS.md is that
// baseline; this closes ADR 0004's gap for the fast path.
//
// Single-producer/single-consumer only: one thread calls try_push()
// (always the same thread once a Stream<T> hands this buffer out via
// ring_for_current_thread(), see stream.hpp), a possibly-different thread
// calls try_pop() (Stream<T>::drain()), but try_pop() is never called
// concurrently with itself. This is the same contract v0.1's mutex-based
// drain_impl() already had (see docs/rfc/0001's "single-consumer at a
// time"), just now enforced by the caller instead of a lock.
//
// Classic bounded circular buffer: capacity_ - 1 usable slots (one slot
// always kept empty to distinguish "full" from "empty" using only head/tail
// equality) -- the standard trick for a wait-free SPSC ring.

namespace chronicle {

template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(std::size_t requested_capacity)
        : capacity_(next_pow2(requested_capacity < 2 ? 2 : requested_capacity)),
          mask_(capacity_ - 1),
          buffer_(capacity_) {}

    RingBuffer(RingBuffer const&) = delete;
    RingBuffer& operator=(RingBuffer const&) = delete;

    // Producer-only. Returns false (leaving `item` untouched -- no move
    // happens) if the buffer is full; the caller decides what "full" means
    // (drop, block, evict) since that's an overflow-policy decision, not a
    // ring buffer one.
    bool try_push(T&& item) {
        auto const head = head_.load(std::memory_order_relaxed);
        auto const next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buffer_[head] = std::move(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer-only.
    bool try_pop(T& out) {
        auto const tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // empty
        }
        out = std::move(buffer_[tail]);
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    static std::size_t next_pow2(std::size_t v) {
        std::size_t p = 1;
        while (p < v) {
            p <<= 1;
        }
        return p;
    }

#if defined(__cpp_lib_hardware_interference_size)
    static constexpr std::size_t kCacheLine = std::hardware_destructive_interference_size;
#else
    static constexpr std::size_t kCacheLine = 64; // typical x86-64/ARM64 line size
#endif

    std::size_t capacity_;
    std::size_t mask_;
    std::vector<T> buffer_;

    // Padded so producer's head_ writes and consumer's tail_ writes never
    // false-share a cache line (docs/09-performance.md calls this out
    // explicitly as a one-line fix with an outsized penalty if missed).
    alignas(kCacheLine) std::atomic<std::size_t> head_{0};
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
};

} // namespace chronicle
