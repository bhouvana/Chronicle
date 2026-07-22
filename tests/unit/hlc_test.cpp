// docs/adr/0019-hybrid-logical-clock.md (v2.0). Covers the primitive
// (HybridLogicalClock::tick()) directly, Session/Stream<T> wiring
// (off by default, on when Session::Config::causal_clock is set), the
// actual cross-stream capability (snapshot_at_hlc() across two different
// tracked<T> fields), and a genuine concurrent stress test -- this
// project has already found real concurrency bugs by testing shared
// atomic state under real contention rather than trusting it by
// inspection alone (docs/adr/0009-lock-free-ring-buffer.md), and this is
// new shared atomic state.

#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <set>
#include <thread>
#include <vector>

using namespace chronicle;

CHRONICLE_TEST(hlc_tick_advances_logical_within_the_same_physical_tick) {
    HybridLogicalClock clock;
    auto const a = clock.tick(100);
    auto const b = clock.tick(100);
    auto const c = clock.tick(100);
    CHRONICLE_CHECK(a.physical_us == 100 && a.logical == 0);
    CHRONICLE_CHECK(b.physical_us == 100 && b.logical == 1);
    CHRONICLE_CHECK(c.physical_us == 100 && c.logical == 2);
    CHRONICLE_CHECK(a < b);
    CHRONICLE_CHECK(b < c);
}

CHRONICLE_TEST(hlc_tick_resets_logical_when_physical_time_advances) {
    HybridLogicalClock clock;
    [[maybe_unused]] auto const a = clock.tick(100);
    [[maybe_unused]] auto const b = clock.tick(100);
    auto const c = clock.tick(200);
    CHRONICLE_CHECK(c.physical_us == 200 && c.logical == 0);
}

CHRONICLE_TEST(hlc_tick_never_goes_backwards_even_if_physical_time_appears_to) {
    // steady_clock reading a smaller value than a previous call (shouldn't
    // happen with a real monotonic clock, but the tick() API takes a raw
    // integer and doesn't get to assume its caller is well-behaved) must
    // still produce a strictly-increasing result, not silently accept a
    // regression.
    HybridLogicalClock clock;
    [[maybe_unused]] auto const a = clock.tick(500);
    auto const b = clock.tick(100); // "earlier" than the previous tick
    CHRONICLE_CHECK(a < b);
    CHRONICLE_CHECK(b.physical_us == 500 && b.logical == 1);
}

CHRONICLE_TEST(default_constructed_hlc_timestamp_is_not_known) {
    CHRONICLE_CHECK(!is_known(HlcTimestamp{}));
    CHRONICLE_CHECK(is_known(HlcTimestamp{1, 0}));
    CHRONICLE_CHECK(is_known(HlcTimestamp{0, 1}));
}

CHRONICLE_TEST(events_have_unknown_hlc_when_causal_clock_is_disabled) {
    Session session; // causal_clock defaults to false
    tracked<int> value{0};
    track(value, session, "field");
    value = 1;
    value = 2;

    for (auto const& record : history(value)) {
        CHRONICLE_CHECK(!is_known(record.hlc));
    }
}

CHRONICLE_TEST(events_get_monotonically_increasing_known_hlc_when_enabled) {
    Session session(Session::Config{RetentionPolicy::ring_window(1024), OverflowPolicy::DropOldest, 64, true});
    tracked<int> value{0};
    track(value, session, "field");
    value = 1;
    value = 2;

    auto const hx = history(value);
    CHRONICLE_CHECK(hx.size() == 3);
    for (auto const& record : hx) {
        CHRONICLE_CHECK(is_known(record.hlc));
    }
    CHRONICLE_CHECK(hx[0].hlc < hx[1].hlc);
    CHRONICLE_CHECK(hx[1].hlc < hx[2].hlc);
}

// The actual point of this feature: an HLC captured from one tracked field
// can be used to query a *different* tracked field's state as of that same
// moment -- something snapshot_at_version() cannot do at all, since two
// different Stream<T>s have independent version counters with no shared
// meaning.
CHRONICLE_TEST(snapshot_at_hlc_answers_cross_stream_queries) {
    Session session(Session::Config{RetentionPolicy::ring_window(1024), OverflowPolicy::DropOldest, 64, true});
    tracked<int> health{100};
    track(health, session, "player.health");
    tracked<std::string> zone{"start"};
    track(zone, session, "player.zone");

    health = 90;
    zone = "forest"; // capture this moment
    auto const forest_hlc = last_writer(zone)->hlc;
    health = 80; // happens after the "forest" moment
    health = 70;

    auto const snap = snapshot_at_hlc(health, forest_hlc);
    CHRONICLE_CHECK(snap.has_value());
    CHRONICLE_CHECK(snap->value == 90); // health as of when the zone changed to "forest"
}

CHRONICLE_TEST(snapshot_at_hlc_returns_nullopt_before_any_qualifying_event) {
    Session session(Session::Config{RetentionPolicy::ring_window(1024), OverflowPolicy::DropOldest, 64, true});
    tracked<int> value{0};
    track(value, session, "field");
    auto const early_hlc = HlcTimestamp{}; // "before time began" -- unknown, never satisfies is_known()
    auto const snap = snapshot_at_hlc(value, early_hlc);
    CHRONICLE_CHECK(!snap.has_value());
}

// Genuine concurrent stress test (docs/adr/0009's precedent: this project
// has found real bugs in shared atomic state only by testing it under real
// contention, not by inspection). Every tick() across every thread must be
// globally distinct and strictly ordered -- if the CAS loop ever lost an
// update, two threads would observe the same (physical_us, logical) pair.
CHRONICLE_TEST(concurrent_hlc_ticks_are_all_distinct_and_strictly_ordered) {
    HybridLogicalClock clock;
    constexpr int num_threads = 8;
    constexpr int ticks_per_thread = 5000;

    std::vector<std::vector<HlcTimestamp>> per_thread_results(num_threads);
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&clock, &per_thread_results, t] {
            auto& results = per_thread_results[static_cast<std::size_t>(t)];
            results.reserve(ticks_per_thread);
            for (int i = 0; i < ticks_per_thread; ++i) {
                results.push_back(clock.tick(static_cast<std::uint64_t>(i)));
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    std::vector<HlcTimestamp> all;
    all.reserve(static_cast<std::size_t>(num_threads) * ticks_per_thread);
    for (auto& results : per_thread_results) {
        all.insert(all.end(), results.begin(), results.end());
    }
    std::sort(all.begin(), all.end(),
              [](HlcTimestamp const& a, HlcTimestamp const& b) { return a < b; });

    CHRONICLE_CHECK(all.size() == static_cast<std::size_t>(num_threads) * ticks_per_thread);
    bool all_distinct_and_ordered = true;
    for (std::size_t i = 1; i < all.size(); ++i) {
        if (!(all[i - 1] < all[i])) {
            all_distinct_and_ordered = false;
            break;
        }
    }
    CHRONICLE_CHECK(all_distinct_and_ordered);
}
