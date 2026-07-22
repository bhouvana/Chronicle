// Every other test file in this suite is single-threaded -- which cannot
// validate the actual claim of this feature (a lock-free per-thread ring
// buffer, docs/adr/0009-lock-free-ring-buffer.md). These tests spawn real
// std::thread producers against a shared Stream<T>, which is the only way
// to exercise the concurrency contract at all. Correctness checks here are
// necessarily about invariants (no lost/duplicated/corrupted events, no
// crash) rather than exact step-by-step tracing, since thread interleaving
// is nondeterministic by design.

#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using chronicle::Session;

namespace {
constexpr int kThreads = 8;
constexpr int kEventsPerThread = 5000;
} // namespace

CHRONICLE_TEST(concurrent_producers_with_concurrent_drain_lose_nothing_under_block) {
    // Block policy promises no drops (docs/rfc/0001's "Block should regain
    // real meaning" note, closed by this feature) -- but only if something
    // is actually draining concurrently, or a producer whose ring is full
    // spins forever with nobody ever freeing a slot. This test provides
    // that concurrent drainer, which is also exactly the scenario the
    // per-thread-ring design exists for.
    Session session(Session::Config{
        chronicle::RetentionPolicy::unbounded(), chronicle::OverflowPolicy::Block, 64});
    auto& stream = session.create_stream<int>("concurrent_counter");

    std::atomic<bool> producers_done{false};
    std::thread drainer([&] {
        while (!producers_done.load(std::memory_order_acquire)) {
            stream.drain();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });

    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&stream, t] {
            for (int i = 0; i < kEventsPerThread; ++i) {
                stream.record(t * kEventsPerThread + i); // globally unique value per event
            }
        });
    }
    for (auto& th : producers) {
        th.join();
    }
    producers_done.store(true, std::memory_order_release);
    drainer.join();
    stream.drain(); // belt-and-suspenders final flush

    auto const hx = stream.history();
    CHRONICLE_CHECK(hx.size() == static_cast<std::size_t>(kThreads) * kEventsPerThread);

    std::vector<bool> seen_value(static_cast<std::size_t>(kThreads) * kEventsPerThread, false);
    bool values_ok = true;
    for (auto const& record : hx) {
        auto const v = static_cast<std::size_t>(record.value);
        if (record.value < 0 || v >= seen_value.size() || seen_value[v]) {
            values_ok = false;
            break;
        }
        seen_value[v] = true;
    }
    CHRONICLE_CHECK(values_ok); // every value present exactly once: no loss, no duplication, no corruption

    std::vector<bool> seen_version(hx.size(), false);
    bool versions_ok = true;
    for (auto const& record : hx) {
        if (record.version >= hx.size() || seen_version[record.version]) {
            versions_ok = false;
            break;
        }
        seen_version[record.version] = true;
    }
    CHRONICLE_CHECK(versions_ok); // versions are a contiguous 0..N-1 permutation, no gaps or duplicates

    bool const sorted = std::is_sorted(hx.begin(), hx.end(), [](auto const& a, auto const& b) {
        return a.version < b.version;
    });
    CHRONICLE_CHECK(sorted); // drain_impl()'s cross-thread merge-sort restored version order (ADR 0003)
}

CHRONICLE_TEST(concurrent_producers_under_drop_oldest_overflow_stay_internally_consistent) {
    // Deliberately small ring + no concurrent drainer: every producer will
    // hit DropOldest's mutex-coordinated slow path (record()'s DropOldest
    // branch in stream.hpp) frequently, from multiple threads at once. This
    // is the trickiest path in the whole design -- the one place a
    // non-drain()ing thread still takes a lock and mutates a ring buffer
    // another thread might be draining. Correctness here means "no crash,
    // no corruption, no duplicate/out-of-range values or versions" -- NOT
    // "every event survives" (DropOldest's entire point is that some don't).
    Session session(Session::Config{
        chronicle::RetentionPolicy::ring_window(16), chronicle::OverflowPolicy::DropOldest, 64});
    auto& stream = session.create_stream<int>("lossy_counter");

    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&stream, t] {
            for (int i = 0; i < kEventsPerThread; ++i) {
                stream.record(t * kEventsPerThread + i);
            }
        });
    }
    for (auto& th : producers) {
        th.join();
    }
    stream.drain();

    auto const hx = stream.history();
    CHRONICLE_CHECK(!hx.empty());
    CHRONICLE_CHECK(hx.size() <= static_cast<std::size_t>(kThreads) * kEventsPerThread);

    std::vector<bool> seen_value(static_cast<std::size_t>(kThreads) * kEventsPerThread, false);
    bool values_ok = true;
    for (auto const& record : hx) {
        auto const v = static_cast<std::size_t>(record.value);
        if (record.value < 0 || v >= seen_value.size() || seen_value[v]) {
            values_ok = false;
            break;
        }
        seen_value[v] = true;
    }
    CHRONICLE_CHECK(values_ok); // every surviving value is in-range and appears at most once

    bool const sorted = std::is_sorted(hx.begin(), hx.end(), [](auto const& a, auto const& b) {
        return a.version < b.version;
    });
    CHRONICLE_CHECK(sorted);

    bool versions_unique = true;
    for (std::size_t i = 1; i < hx.size(); ++i) {
        if (hx[i].version == hx[i - 1].version) {
            versions_unique = false;
            break;
        }
    }
    CHRONICLE_CHECK(versions_unique);
}
