// docs/13-vision.md Layer 4, docs/adr/0033-derived-state.md.
// chronicle::derive()/explain(): the vision doc's own example,
// gold = income - tax, auto-recomputed and auto-explained.

#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

using namespace chronicle;

CHRONICLE_TEST(derive_recomputes_and_records_when_a_dependency_changes) {
    Session session;
    tracked<int> income{100};
    tracked<int> tax{20};
    tracked<int> gold{0};
    track(income, session, "income");
    track(tax, session, "tax");
    track(gold, session, "gold");

    auto binding = derive<int, int, int>(
        gold, [](int const& i, int const& t) { return i - t; }, income, tax);

    income = 110;
    CHRONICLE_CHECK(gold.get() == 90); // 110 - 20, recomputed automatically

    auto const explanation = explain(gold);
    CHRONICLE_CHECK(explanation.has_value());
    CHRONICLE_CHECK(explanation->size() == 2);
    bool found_income_changed = false;
    bool found_tax_unchanged = false;
    for (auto const& change : *explanation) {
        if (change.name == "income") {
            CHRONICLE_CHECK(change.changed);
            CHRONICLE_CHECK(change.old_value == "100");
            CHRONICLE_CHECK(change.new_value == "110");
            found_income_changed = true;
        }
        if (change.name == "tax") {
            CHRONICLE_CHECK(!change.changed);
            found_tax_unchanged = true;
        }
    }
    CHRONICLE_CHECK(found_income_changed);
    CHRONICLE_CHECK(found_tax_unchanged);
}

CHRONICLE_TEST(derive_attribution_flips_to_whichever_dependency_actually_changed) {
    Session session;
    tracked<int> income{100};
    tracked<int> tax{20};
    tracked<int> gold{0};
    track(income, session, "income");
    track(tax, session, "tax");
    track(gold, session, "gold");

    auto binding = derive<int, int, int>(
        gold, [](int const& i, int const& t) { return i - t; }, income, tax);

    tax = 30;
    CHRONICLE_CHECK(gold.get() == 70); // 100 - 30

    auto const explanation = explain(gold);
    CHRONICLE_CHECK(explanation.has_value());
    for (auto const& change : *explanation) {
        if (change.name == "tax") {
            CHRONICLE_CHECK(change.changed);
        }
        if (change.name == "income") {
            CHRONICLE_CHECK(!change.changed);
        }
    }
}

CHRONICLE_TEST(derived_target_behaves_like_an_ordinary_tracked_field) {
    Session session;
    tracked<int> income{100};
    tracked<int> tax{20};
    tracked<int> gold{0};
    track(income, session, "income");
    track(tax, session, "tax");
    track(gold, session, "gold");

    auto binding = derive<int, int, int>(
        gold, [](int const& i, int const& t) { return i - t; }, income, tax);

    income = 110;
    income = 120;

    auto const hx = history(gold);
    // version 0 (track()'s initial record) + 2 recomputations
    CHRONICLE_CHECK(hx.size() == 3);
    CHRONICLE_CHECK(hx[0].value == 0);
    CHRONICLE_CHECK(hx[1].value == 90);
    CHRONICLE_CHECK(hx[2].value == 100);
}

CHRONICLE_TEST(destroying_the_derivation_handle_stops_recomputation) {
    Session session;
    tracked<int> income{100};
    tracked<int> tax{20};
    tracked<int> gold{0};
    track(income, session, "income");
    track(tax, session, "tax");
    track(gold, session, "gold");

    {
        auto binding = derive<int, int, int>(
            gold, [](int const& i, int const& t) { return i - t; }, income, tax);
        income = 110;
        CHRONICLE_CHECK(gold.get() == 90);
    } // binding destroyed here -- hooks must detach

    income = 500; // gold must NOT recompute anymore
    CHRONICLE_CHECK(gold.get() == 90);
}

// docs/adr/0040-composable-record-hooks.md: the actual composability proof
// this change exists for. A manual second hook (standing in for what
// chronicle::tracy_bridge::PlotHandle would attach in real use -- Tracy
// itself isn't a dependency of this test target) is attached to the same
// dependency stream a Derivation is already watching; both must fire on
// one mutation, neither displacing the other, the way set_record_hook()'s
// old single-slot behavior would have.
CHRONICLE_TEST(a_manually_attached_second_hook_coexists_with_a_derivation) {
    Session session;
    tracked<int> income{100};
    tracked<int> tax{20};
    tracked<int> gold{0};
    track(income, session, "income");
    track(tax, session, "tax");
    track(gold, session, "gold");

    auto binding = derive<int, int, int>(
        gold, [](int const& i, int const& t) { return i - t; }, income, tax);

    int observed_by_second_hook = 0;
    auto* income_stream = income.stream();
    auto const second_hook_handle = income_stream->add_record_hook(
        [](void* ctx, int const& value, std::source_location const&) {
            *static_cast<int*>(ctx) = value;
        },
        &observed_by_second_hook);

    income = 300;

    CHRONICLE_CHECK(gold.get() == 280);               // the Derivation's own hook still fired
    CHRONICLE_CHECK(observed_by_second_hook == 300);   // the manually-attached hook ALSO fired

    income_stream->remove_record_hook(second_hook_handle);
}
