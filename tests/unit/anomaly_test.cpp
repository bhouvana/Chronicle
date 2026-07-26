// docs/12-future-research-topics.md topic 8, docs/adr/0026-anomaly-detection.md:
// chronicle::range_anomalies() -- an online z-score against a field's own
// running mean/stddev, causal (never uses future events to judge a past
// one).

#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

using namespace chronicle;

CHRONICLE_TEST(range_anomalies_finds_nothing_below_min_samples) {
    Session session;
    tracked<int> value{0};
    track(value, session, "field");
    value = 1000; // a huge jump, but only the 2nd event -- below default min_samples
    CHRONICLE_CHECK(range_anomalies(history(value)).empty());
}

CHRONICLE_TEST(range_anomalies_flags_a_real_outlier_against_stable_history) {
    Session session;
    tracked<double> value{10.0};
    track(value, session, "field");
    for (int i = 0; i < 10; ++i) {
        value = 10.0 + (i % 2 == 0 ? 0.1 : -0.1); // small, stable noise around 10
    }
    value = 500.0; // real outlier

    auto const anomalies = range_anomalies(history(value), /*z_threshold=*/3.0, /*min_samples=*/3);
    CHRONICLE_CHECK(!anomalies.empty());
    CHRONICLE_CHECK(anomalies.back().index == history(value).size() - 1);
    CHRONICLE_CHECK(anomalies.back().value == 500.0);
}

CHRONICLE_TEST(range_anomalies_does_not_flag_consistent_values) {
    Session session;
    tracked<int> value{42};
    track(value, session, "field");
    for (int i = 0; i < 20; ++i) {
        value = 42; // never changes
    }
    CHRONICLE_CHECK(range_anomalies(history(value)).empty());
}

CHRONICLE_TEST(range_anomalies_flags_zero_variance_break_without_dividing_by_zero) {
    Session session;
    tracked<int> value{7};
    track(value, session, "field");
    for (int i = 0; i < 5; ++i) {
        value = 7; // identical -- zero observed variance
    }
    value = 8; // first-ever different value

    auto const anomalies = range_anomalies(history(value));
    CHRONICLE_CHECK(!anomalies.empty());
    CHRONICLE_CHECK(anomalies.back().value == 8);
}

CHRONICLE_TEST(range_anomalies_is_causal_not_hindsight_scored) {
    // An early outlier must still be flagged even though it gets "averaged
    // out" by a long run of normal values after it -- a whole-history batch
    // z-score could hide this; a causal, online one cannot.
    Session session;
    tracked<double> value{0.0};
    track(value, session, "field");
    value = 1.0;
    value = 1.0;
    value = 100.0; // outlier, early in the series
    for (int i = 0; i < 30; ++i) {
        value = 1.0; // long run of "normal" afterward
    }

    auto const anomalies = range_anomalies(history(value));
    bool found_the_outlier = false;
    for (auto const& a : anomalies) {
        if (a.value == 100.0) found_the_outlier = true;
    }
    CHRONICLE_CHECK(found_the_outlier);
}

// docs/13-vision.md Layer 8's "size always grows, never shrinks... possible
// leak" example -- chronicle::container_growth_report()/is_likely_leak().

CHRONICLE_TEST(container_growth_report_tracks_size_through_inserts_and_erases) {
    Session session;
    tracked_vector<int> buffer;
    track(buffer, session, "buffer");
    buffer.push_back(1);
    buffer.push_back(2);
    buffer.push_back(3);
    buffer.erase(0);

    auto const report = container_growth_report(history(buffer));
    CHRONICLE_CHECK(report.max_size == 3);
    CHRONICLE_CHECK(report.final_size == 2);
    CHRONICLE_CHECK(report.insert_count == 3);
    CHRONICLE_CHECK(report.erase_or_clear_count == 1);
    CHRONICLE_CHECK(report.ever_shrunk);
}

CHRONICLE_TEST(is_likely_leak_flags_a_container_that_never_shrinks_past_the_threshold) {
    Session session;
    tracked_vector<int> buffer;
    track(buffer, session, "buffer");
    for (int i = 0; i < 15; ++i) {
        buffer.push_back(i); // never erased -- a real "never shrinks" shape
    }

    auto const report = container_growth_report(history(buffer));
    CHRONICLE_CHECK(is_likely_leak(report));         // default threshold (10) -- 15 >= 10
    CHRONICLE_CHECK(!is_likely_leak(report, 100));   // explicit higher threshold -- not flagged
}

CHRONICLE_TEST(is_likely_leak_does_not_flag_a_container_that_has_shrunk) {
    Session session;
    tracked_vector<int> buffer;
    track(buffer, session, "buffer");
    for (int i = 0; i < 15; ++i) {
        buffer.push_back(i);
    }
    buffer.erase(0); // one real shrink is enough to disqualify "never shrinks"

    auto const report = container_growth_report(history(buffer));
    CHRONICLE_CHECK(report.ever_shrunk);
    CHRONICLE_CHECK(!is_likely_leak(report));
}
