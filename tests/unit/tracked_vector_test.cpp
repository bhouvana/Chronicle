#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

using chronicle::ContainerOpKind;
using chronicle::Session;

CHRONICLE_TEST(untracked_vector_behaves_like_a_plain_vector) {
    chronicle::tracked_vector<int> items;
    items.push_back(1);
    items.push_back(2);
    CHRONICLE_CHECK(items.size() == 2);
    CHRONICLE_CHECK(items[0] == 1);
    CHRONICLE_CHECK(items[1] == 2);
    CHRONICLE_CHECK(items.stream() == nullptr);
    CHRONICLE_CHECK(chronicle::history(items).empty());
}

CHRONICLE_TEST(tracking_records_pre_existing_elements_as_inserts) {
    Session session;
    chronicle::tracked_vector<int> items;
    items.push_back(10);
    items.push_back(20);
    chronicle::track(items, session, "items");

    auto hx = chronicle::history(items);
    CHRONICLE_CHECK(hx.size() == 2);
    CHRONICLE_CHECK(hx[0].value.kind == ContainerOpKind::Insert);
    CHRONICLE_CHECK(hx[0].value.value == 10);
    CHRONICLE_CHECK(hx[1].value.value == 20);
}

CHRONICLE_TEST(push_back_erase_update_clear_are_all_recorded) {
    Session session;
    chronicle::tracked_vector<int> items;
    chronicle::track(items, session, "items");

    items.push_back(1);
    items.push_back(2);
    items.push_back(3);
    items.update(1, 20);
    items.erase(0);
    // items is now [20, 3]
    CHRONICLE_CHECK(items.size() == 2);
    CHRONICLE_CHECK(items[0] == 20);
    CHRONICLE_CHECK(items[1] == 3);

    auto hx = chronicle::history(items);
    CHRONICLE_CHECK(hx.size() == 5); // 3 inserts + 1 update + 1 erase
    CHRONICLE_CHECK(hx[3].value.kind == ContainerOpKind::Update);
    CHRONICLE_CHECK(hx[4].value.kind == ContainerOpKind::Erase);

    items.clear();
    CHRONICLE_CHECK(items.empty());
    hx = chronicle::history(items);
    CHRONICLE_CHECK(hx.size() == 6);
    CHRONICLE_CHECK(hx[5].value.kind == ContainerOpKind::Clear);
}

CHRONICLE_TEST(snapshot_reconstructs_container_contents_via_replay) {
    Session session;
    chronicle::tracked_vector<int> items;
    chronicle::track(items, session, "items");

    // Version-bounded, not timestamp-bounded: back-to-back operations can
    // land on the same std::chrono::steady_clock tick under optimization
    // (confirmed in practice -- see docs/adr/0007-timestamp-ties-under-
    // optimization.md), so a test asserting exact reconstruction must use
    // the one ordering the stream actually guarantees: version.
    items.push_back(1);
    items.push_back(2);
    auto const v0 = chronicle::current_version(items);
    items.push_back(3);
    items.update(0, 100);
    auto const v1 = chronicle::current_version(items);
    items.erase(1);

    auto snap0 = chronicle::snapshot_at_version(items, v0);
    CHRONICLE_CHECK(snap0.has_value());
    CHRONICLE_CHECK(snap0->value.size() == 2);
    CHRONICLE_CHECK(snap0->value[0] == 1);
    CHRONICLE_CHECK(snap0->value[1] == 2);

    auto snap1 = chronicle::snapshot_at_version(items, v1);
    CHRONICLE_CHECK(snap1.has_value());
    CHRONICLE_CHECK(snap1->value.size() == 3);
    CHRONICLE_CHECK(snap1->value[0] == 100);
    CHRONICLE_CHECK(snap1->value[1] == 2);
    CHRONICLE_CHECK(snap1->value[2] == 3);

    // Live state reflects the later erase(1) that happened after t1.
    CHRONICLE_CHECK(items.size() == 2);
    CHRONICLE_CHECK(items[0] == 100);
    CHRONICLE_CHECK(items[1] == 3);
}

CHRONICLE_TEST(container_diff_reports_updates_inserts_and_erases) {
    Session session;
    chronicle::tracked_vector<int> items;
    chronicle::track(items, session, "items");

    items.push_back(1);
    items.push_back(2);
    items.push_back(3);
    auto const v0 = chronicle::current_version(items);

    items.update(0, 10);   // update at index 0
    items.push_back(4);    // insert at index 3
    auto const v1 = chronicle::current_version(items);

    auto snap0 = chronicle::snapshot_at_version(items, v0);
    auto snap1 = chronicle::snapshot_at_version(items, v1);
    CHRONICLE_CHECK(snap0.has_value());
    CHRONICLE_CHECK(snap1.has_value());

    auto d = chronicle::diff(*snap0, *snap1);
    CHRONICLE_CHECK(d.changes.size() == 2);

    bool saw_update = false;
    bool saw_insert = false;
    for (auto const& change : d) {
        if (change.kind == ContainerOpKind::Update) {
            CHRONICLE_CHECK(change.index == 0);
            CHRONICLE_CHECK(change.before == 1);
            CHRONICLE_CHECK(change.after == 10);
            saw_update = true;
        } else if (change.kind == ContainerOpKind::Insert) {
            CHRONICLE_CHECK(change.index == 3);
            CHRONICLE_CHECK(change.after == 4);
            saw_insert = true;
        }
    }
    CHRONICLE_CHECK(saw_update);
    CHRONICLE_CHECK(saw_insert);
}
