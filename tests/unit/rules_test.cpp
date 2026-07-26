// docs/adr/0041-doctor-and-rules.md: chronicle::rules::check_rule() (offline)
// and chronicle::watch() (live) -- "did the program violate any
// expectations," not just "what happened."

#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

using namespace chronicle;

CHRONICLE_TEST(check_rule_finds_every_violation_in_recorded_history) {
    Session session;
    tracked<int> hp{100};
    track(hp, session, "hp");
    hp = 50;
    hp = -10; // violation: hp >= 0
    hp = 20;
    hp = -5;  // violation

    auto const violations = rules::check_rule(history(hp), [](int const& v) { return v >= 0; });
    CHRONICLE_CHECK(violations.size() == 2);
    CHRONICLE_CHECK(violations[0].value == -10);
    CHRONICLE_CHECK(violations[1].value == -5);
}

CHRONICLE_TEST(check_rule_finds_nothing_when_the_rule_never_breaks) {
    Session session;
    tracked<int> hp{100};
    track(hp, session, "hp");
    hp = 50;
    hp = 20;

    auto const violations = rules::check_rule(history(hp), [](int const& v) { return v >= 0; });
    CHRONICLE_CHECK(violations.empty());
}

CHRONICLE_TEST(watch_fires_the_callback_live_on_a_real_violation) {
    Session session;
    tracked<int> hp{100};
    track(hp, session, "hp");

    int violation_count = 0;
    int last_bad_value = 0;
    auto handle = watch<int>(
        hp, [](int const& v) { return v >= 0; },
        [&](int const& v, std::source_location const&) {
            ++violation_count;
            last_bad_value = v;
        });

    hp = 50; // valid -- no callback
    CHRONICLE_CHECK(violation_count == 0);

    hp = -10; // violates -- callback fires
    CHRONICLE_CHECK(violation_count == 1);
    CHRONICLE_CHECK(last_bad_value == -10);

    hp = 30; // valid again -- no new callback
    CHRONICLE_CHECK(violation_count == 1);
}

CHRONICLE_TEST(destroying_the_watch_handle_stops_the_callback) {
    Session session;
    tracked<int> hp{100};
    track(hp, session, "hp");

    int violation_count = 0;
    {
        auto handle = watch<int>(
            hp, [](int const& v) { return v >= 0; },
            [&](int const& v, std::source_location const&) { ++violation_count; });
        hp = -1;
        CHRONICLE_CHECK(violation_count == 1);
    } // handle destroyed -- hook must detach

    hp = -2; // must NOT fire the (now-dangling) callback
    CHRONICLE_CHECK(violation_count == 1);
}

// docs/adr/0040-composable-record-hooks.md's whole point, exercised here
// too: a watch() and a derive() binding on the same field must coexist.
CHRONICLE_TEST(watch_coexists_with_a_derivation_on_the_same_dependency) {
    Session session;
    tracked<int> income{100};
    tracked<int> tax{20};
    tracked<int> gold{0};
    track(income, session, "income");
    track(tax, session, "tax");
    track(gold, session, "gold");

    auto binding = derive<int, int, int>(
        gold, [](int const& i, int const& t) { return i - t; }, income, tax);

    bool went_negative = false;
    auto watch_handle = watch<int>(
        income, [](int const& v) { return v >= 0; },
        [&](int const&, std::source_location const&) { went_negative = true; });

    income = 150;
    CHRONICLE_CHECK(gold.get() == 130);  // Derivation's hook still fired
    CHRONICLE_CHECK(!went_negative);     // watch's hook also ran, correctly found no violation

    income = -5;
    CHRONICLE_CHECK(gold.get() == -25);  // Derivation still fires
    CHRONICLE_CHECK(went_negative);      // watch's hook correctly caught the violation
}
