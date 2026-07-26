#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "chronicle/tracked.hpp"

// docs/12-future-research-topics.md topic 3, docs/adr/0029-memcpy-interposition.md.
// Header-only, zero-dependency, always available: chronicle-core itself
// never links Detours/libc-interposition machinery, only the opt-in
// tools/memcpy-shim module does. Watching zero fields costs one empty
// std::vector -- no cost for anyone who never calls watch().
//
// This is the "address-range filtering" work docs/12's post-v2.0 spike
// findings identified as real, additional, not-yet-attempted scope: the
// original spike's hook fired on *every* memcpy in the process (including
// an unrelated internal CRT call), making it a firehose rather than a
// usable signal. This registry is what a hook checks before reporting
// anything, closing that specific gap -- it does NOT change the
// spike's other, structural finding (compile-time-constant-size copies
// are invisible to any libc-level hook, confirmed at the assembly level;
// see ADR 0029), which no amount of address filtering can fix.

namespace chronicle::interposition {

class Registry {
public:
    static Registry& instance() {
        static Registry registry;
        return registry;
    }

    void watch(void const* addr, std::size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto const begin = reinterpret_cast<std::uintptr_t>(addr);
        ranges_.push_back(Range{begin, begin + size});
    }

    void unwatch(void const* addr) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto const begin = reinterpret_cast<std::uintptr_t>(addr);
        ranges_.erase(std::remove_if(ranges_.begin(), ranges_.end(),
                                      [&](Range const& r) { return r.begin == begin; }),
                       ranges_.end());
    }

    // True if [addr, addr+size) overlaps any watched range at all --
    // linear scan is deliberate: this runs inside a hooked libc call
    // (tools/memcpy-shim), a cold/rare path relative to the copies it's
    // filtering, not chronicle-core's own record() hot path, so an O(n)
    // scan over a typically-small watch list is the right trade, same
    // reasoning ADR 0019's snapshot_at_hlc() already used for its own
    // full-scan query.
    [[nodiscard]] bool overlaps(void const* addr, std::size_t size) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto const begin = reinterpret_cast<std::uintptr_t>(addr);
        auto const end = begin + size;
        for (auto const& r : ranges_) {
            if (begin < r.end && r.begin < end) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::size_t watched_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ranges_.size();
    }

private:
    struct Range {
        std::uintptr_t begin;
        std::uintptr_t end;
    };
    mutable std::mutex mutex_;
    std::vector<Range> ranges_;
};

// Convenience wrapper: watches a tracked<T>'s backing storage via its
// already-public get() accessor -- no new method needed on tracked<T>
// itself, and this stays a fully separate, opt-in call a caller makes
// explicitly, never something record()'s hot path touches.
template <typename T>
void watch(tracked<T> const& field) {
    Registry::instance().watch(&field.get(), sizeof(T));
}

template <typename T>
void unwatch(tracked<T> const& field) {
    Registry::instance().unwatch(&field.get());
}

} // namespace chronicle::interposition
