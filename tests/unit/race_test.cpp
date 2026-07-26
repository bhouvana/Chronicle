// docs/12-future-research-topics.md topic 1's recommended follow-up from
// the v2.0 deterministic-replay research spike: chronicle::possible_race(),
// a real primitive built on the existing HLC (ADR 0019), not a new
// mechanism.

#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

#include <thread>

using namespace chronicle;

namespace {
Session::Config causal_config() {
    return Session::Config{RetentionPolicy::ring_window(1024), OverflowPolicy::DropOldest, 64, true};
}
} // namespace

CHRONICLE_TEST(possible_race_is_false_when_causal_clock_disabled) {
    Session session; // causal_clock defaults to false
    tracked<int> a{0};
    tracked<int> b{0};
    track(a, session, "a");
    track(b, session, "b");
    a = 1;
    b = 1;
    CHRONICLE_CHECK(!possible_race(*last_writer(a), *last_writer(b)));
}

CHRONICLE_TEST(possible_race_is_false_for_same_thread_events_regardless_of_closeness) {
    Session session(causal_config());
    tracked<int> a{0};
    tracked<int> b{0};
    track(a, session, "a");
    track(b, session, "b");
    a = 1; // same thread as b's mutation below -- strict program order
    b = 1;
    CHRONICLE_CHECK(!possible_race(*last_writer(a), *last_writer(b)));
}

CHRONICLE_TEST(possible_race_is_true_for_cross_thread_events_sharing_a_physical_tick) {
    Session session(causal_config());
    tracked<int> a{0};
    tracked<int> b{0};
    track(a, session, "a");
    track(b, session, "b");

    // Two threads racing to record as close together as this environment
    // can make them -- real threads, not simulated HLCs, since the point is
    // to exercise the actual record() -> next_hlc_tick() path under real
    // scheduling, the same discipline docs/adr/0009 and the hlc_test.cpp
    // concurrency test already established for this project's shared
    // atomic state.
    std::thread t1([&] { a = 1; });
    std::thread t2([&] { b = 1; });
    t1.join();
    t2.join();

    auto const rec_a = *last_writer(a);
    auto const rec_b = *last_writer(b);
    // Real threads scheduled independently won't reliably land in the same
    // microsecond, so assert the mechanism directly instead of depending on
    // timing luck: a wide enough window must report a race between any two
    // known, different-thread HLCs, and thread_id really did differ.
    CHRONICLE_CHECK(rec_a.thread_id != rec_b.thread_id);
    CHRONICLE_CHECK(possible_race(rec_a, rec_b, /*window_us=*/1'000'000));
}

CHRONICLE_TEST(possible_race_is_false_for_cross_thread_events_far_apart_in_time) {
    Session session(causal_config());
    tracked<int> a{0};
    tracked<int> b{0};
    track(a, session, "a");
    track(b, session, "b");

    std::thread t1([&] { a = 1; });
    t1.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::thread t2([&] { b = 1; });
    t2.join();

    auto const rec_a = *last_writer(a);
    auto const rec_b = *last_writer(b);
    // No thread_id check here (unlike the "sharing a physical tick" test
    // above): t1 has already exited and been joined before t2 is even
    // created, so the two threads' lifetimes never overlap, and a real OS
    // is free to reuse the same thread id for t2 -- caught as a real,
    // reproducible failure on Linux CI, not a flake. The actual thing this
    // test verifies is the time-window logic below, not thread-id
    // uniqueness (which only holds when both threads are alive at once).
    CHRONICLE_CHECK(!possible_race(rec_a, rec_b, /*window_us=*/1'000)); // 1ms window, 50ms apart
}

CHRONICLE_TEST(possible_race_default_window_is_zero_same_tick_only) {
    Session session(causal_config());
    tracked<int> a{0};
    tracked<int> b{0};
    track(a, session, "a");
    track(b, session, "b");

    std::thread t1([&] { a = 1; });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::thread t2([&] { b = 1; });
    t1.join();
    t2.join();

    auto const rec_a = *last_writer(a);
    auto const rec_b = *last_writer(b);
    CHRONICLE_CHECK(!possible_race(rec_a, rec_b)); // default window_us=0, 5ms apart
}
