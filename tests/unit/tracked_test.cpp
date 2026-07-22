#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

using chronicle::OverflowPolicy;
using chronicle::RetentionPolicy;
using chronicle::Session;

CHRONICLE_TEST(untracked_field_behaves_like_a_plain_value) {
    chronicle::tracked<int> health{100};
    health = 75;
    CHRONICLE_CHECK(health.get() == 75);
    CHRONICLE_CHECK(health.stream() == nullptr);
    CHRONICLE_CHECK(chronicle::history(health).empty());
}

CHRONICLE_TEST(tracking_records_initial_value_as_version_zero) {
    Session session;
    chronicle::tracked<int> health{100};
    chronicle::track(health, session, "health");

    auto hx = chronicle::history(health);
    CHRONICLE_CHECK(hx.size() == 1);
    CHRONICLE_CHECK(hx.front().value == 100);
    CHRONICLE_CHECK(hx.front().version == 0);
}

CHRONICLE_TEST(mutation_appends_to_history_in_order) {
    Session session;
    chronicle::tracked<int> health{100};
    chronicle::track(health, session, "health");

    health = 75;
    health = 50;
    health = 0;

    auto hx = chronicle::history(health);
    CHRONICLE_CHECK(hx.size() == 4); // initial + 3 mutations
    int expected[] = {100, 75, 50, 0};
    std::size_t i = 0;
    for (auto const& record : hx) {
        CHRONICLE_CHECK(record.value == expected[i]);
        CHRONICLE_CHECK(record.version == i);
        ++i;
    }
}

CHRONICLE_TEST(ring_window_retention_evicts_oldest_events) {
    Session session(Session::Config{RetentionPolicy::ring_window(3), OverflowPolicy::DropOldest, 64});
    chronicle::tracked<int> counter{0};
    chronicle::track(counter, session, "counter");

    for (int i = 1; i <= 10; ++i) {
        counter = i;
    }

    auto hx = chronicle::history(counter);
    CHRONICLE_CHECK(hx.size() == 3);
    CHRONICLE_CHECK(hx.front().value == 8);
    CHRONICLE_CHECK(hx.back().value == 10);
}

CHRONICLE_TEST(drop_newest_keeps_earliest_events_when_pending_overflows) {
    // Overflow only triggers when pending exceeds the ring window before a
    // drain happens; drive many mutations without an intervening history()
    // call so they all land in the pending stage at once.
    //
    // Uses ring_window(3), not (2): the staging ring (include/chronicle/
    // ring_buffer.hpp) is power-of-two sized for cheap masking on the hot
    // path (docs/09-performance.md), so its usable capacity is "at least
    // the requested retention," not "exactly" -- requesting 2 actually
    // yields 3 usable slots after rounding, which would make this test's
    // assertions about exactly-2-survive wrong for reasons that have
    // nothing to do with what the test means to exercise. 3 is itself
    // already an exact fit (rounds to a capacity of 4, i.e. 3 usable slots)
    // so the test's numbers mean what they say. See
    // docs/adr/0009-lock-free-ring-buffer.md.
    Session session(Session::Config{RetentionPolicy::ring_window(3), OverflowPolicy::DropNewest, 64});
    chronicle::tracked<int> counter{0};
    chronicle::track(counter, session, "counter");

    for (int i = 1; i <= 5; ++i) {
        counter = i;
    }

    auto hx = chronicle::history(counter);
    CHRONICLE_CHECK(hx.size() == 3);
    CHRONICLE_CHECK(hx.front().value == 0); // initial value survives: DropNewest keeps the earliest
    CHRONICLE_CHECK(hx.back().value == 2);
}

CHRONICLE_TEST(snapshot_and_diff_report_changes_between_two_versions) {
    // Version-bounded, not timestamp-bounded -- see the same rationale in
    // tracked_vector_test.cpp and docs/adr/0007-timestamp-ties-under-
    // optimization.md. A timestamp captured between two fast mutations can
    // tie with one of them under optimization; version numbers cannot.
    Session session;
    chronicle::tracked<int> score{0};
    chronicle::track(score, session, "score");

    auto const v0 = chronicle::current_version(score);
    score = 10;
    score = 20;
    auto const v2 = chronicle::current_version(score);

    auto snap0 = chronicle::snapshot_at_version(score, v0);
    auto snap2 = chronicle::snapshot_at_version(score, v2);
    CHRONICLE_CHECK(snap0.has_value());
    CHRONICLE_CHECK(snap2.has_value());
    CHRONICLE_CHECK(snap0->value == 0);
    CHRONICLE_CHECK(snap2->value == 20);

    auto d = chronicle::diff(*snap0, *snap2);
    CHRONICLE_CHECK(d.before == 0);
    CHRONICLE_CHECK(d.after == 20);
    CHRONICLE_CHECK(d.changed);
}

CHRONICLE_TEST(timestamp_based_snapshot_still_works_as_a_best_effort_smoke_test) {
    // Keeps snapshot_at(time_point) itself under test, but only asserts
    // things that hold even if two events tie on the same clock reading:
    // a snapshot exists, and its value is one of the values score legally
    // held (never something else entirely, which would indicate a replay
    // bug rather than a clock-resolution tie).
    Session session;
    chronicle::tracked<int> score{0};
    chronicle::track(score, session, "score");
    score = 10;
    score = 20;

    auto snap = chronicle::snapshot(score, std::chrono::steady_clock::now());
    CHRONICLE_CHECK(snap.has_value());
    CHRONICLE_CHECK(snap->value == 0 || snap->value == 10 || snap->value == 20);
}
