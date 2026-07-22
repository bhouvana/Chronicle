// docs/10-roadmap.md's v0.5 "causal-chain queries" item, docs/adr/0010-call-
// site-capture.md's design: plain `field = value` cannot capture a call
// site (a hard C++ language constraint on operator=, confirmed by compiler
// error during this feature's own development, not assumed) -- only the
// named-method paths (chronicle::set(), track(), push_back(), etc.) can.

#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

#include <string>

using chronicle::is_known;
using chronicle::Session;

namespace {
bool ends_with(std::string const& s, std::string const& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
} // namespace

CHRONICLE_TEST(plain_assignment_does_not_capture_call_site) {
    Session session;
    chronicle::tracked<int> health{100};
    chronicle::track(health, session, "health");

    health = 75; // plain operator=, no call site possible

    auto const last = chronicle::last_writer(health);
    CHRONICLE_CHECK(last.has_value());
    CHRONICLE_CHECK(last->value == 75);
    CHRONICLE_CHECK(!is_known(last->call_site));
}

CHRONICLE_TEST(chronicle_set_captures_call_site) {
    Session session;
    chronicle::tracked<int> health{100};
    chronicle::track(health, session, "health");

    chronicle::set(health, 75); // this exact line should be captured

    auto const last = chronicle::last_writer(health);
    CHRONICLE_CHECK(last.has_value());
    CHRONICLE_CHECK(last->value == 75);
    CHRONICLE_CHECK(is_known(last->call_site));
    CHRONICLE_CHECK(ends_with(last->call_site.file_name(), "call_site_test.cpp"));
    CHRONICLE_CHECK(last->call_site.line() > 0);
}

CHRONICLE_TEST(track_itself_captures_a_call_site_for_the_initial_value) {
    Session session;
    chronicle::tracked<int> health{100};
    chronicle::track(health, session, "health"); // track()'s own call site

    auto hx = chronicle::history(health);
    CHRONICLE_CHECK(hx.size() == 1);
    CHRONICLE_CHECK(is_known(hx.front().call_site));
    CHRONICLE_CHECK(ends_with(hx.front().call_site.file_name(), "call_site_test.cpp"));
}

CHRONICLE_TEST(last_writer_matches_history_back) {
    Session session;
    chronicle::tracked<int> counter{0};
    chronicle::track(counter, session, "counter");
    chronicle::set(counter, 1);
    chronicle::set(counter, 2);
    chronicle::set(counter, 3);

    auto hx = chronicle::history(counter);
    auto const last = chronicle::last_writer(counter);
    CHRONICLE_CHECK(last.has_value());
    CHRONICLE_CHECK(last->version == hx.back().version);
    CHRONICLE_CHECK(last->value == hx.back().value);
    CHRONICLE_CHECK(last->value == 3);
}

CHRONICLE_TEST(last_writer_on_untracked_field_is_nullopt) {
    chronicle::tracked<int> value{0};
    CHRONICLE_CHECK(!chronicle::last_writer(value).has_value());
}

CHRONICLE_TEST(tracked_vector_operations_capture_call_site) {
    Session session;
    chronicle::tracked_vector<int> items;
    chronicle::track(items, session, "items");

    items.push_back(1);
    auto push_last = chronicle::last_writer(items);
    CHRONICLE_CHECK(push_last.has_value());
    CHRONICLE_CHECK(is_known(push_last->call_site));
    CHRONICLE_CHECK(ends_with(push_last->call_site.file_name(), "call_site_test.cpp"));

    items.update(0, 2);
    auto update_last = chronicle::last_writer(items);
    CHRONICLE_CHECK(is_known(update_last->call_site));

    items.erase(0);
    auto erase_last = chronicle::last_writer(items);
    CHRONICLE_CHECK(is_known(erase_last->call_site));

    items.push_back(3);
    items.clear();
    auto clear_last = chronicle::last_writer(items);
    CHRONICLE_CHECK(is_known(clear_last->call_site));
}

CHRONICLE_TEST(tracked_map_operations_capture_call_site) {
    Session session;
    chronicle::tracked_map<std::string, int> scores;
    chronicle::track(scores, session, "scores");

    scores.set("alice", 10);
    auto set_last = chronicle::last_writer(scores);
    CHRONICLE_CHECK(set_last.has_value());
    CHRONICLE_CHECK(is_known(set_last->call_site));
    CHRONICLE_CHECK(ends_with(set_last->call_site.file_name(), "call_site_test.cpp"));

    scores.erase("alice");
    auto erase_last = chronicle::last_writer(scores);
    CHRONICLE_CHECK(is_known(erase_last->call_site));
}
