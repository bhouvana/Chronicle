#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

#include <string>

using chronicle::ContainerOpKind;
using chronicle::Session;

CHRONICLE_TEST(untracked_map_behaves_like_a_plain_map) {
    chronicle::tracked_map<std::string, int> scores;
    scores.set("alice", 10);
    scores.set("bob", 20);
    CHRONICLE_CHECK(scores.size() == 2);
    CHRONICLE_CHECK(*scores.find("alice") == 10);
    CHRONICLE_CHECK(scores.stream() == nullptr);
    CHRONICLE_CHECK(chronicle::history(scores).empty());
}

CHRONICLE_TEST(tracking_records_pre_existing_entries_as_inserts) {
    Session session;
    chronicle::tracked_map<std::string, int> scores;
    scores.set("alice", 10);
    chronicle::track(scores, session, "scores");

    auto hx = chronicle::history(scores);
    CHRONICLE_CHECK(hx.size() == 1);
    CHRONICLE_CHECK(hx[0].value.kind == ContainerOpKind::Insert);
    CHRONICLE_CHECK(hx[0].value.key == "alice");
    CHRONICLE_CHECK(hx[0].value.value == 10);
}

CHRONICLE_TEST(set_records_insert_for_new_key_and_update_for_existing_key) {
    Session session;
    chronicle::tracked_map<std::string, int> scores;
    chronicle::track(scores, session, "scores");

    scores.set("alice", 10);   // new key -> Insert
    scores.set("alice", 15);   // existing key -> Update
    scores.set("bob", 20);     // new key -> Insert
    scores.erase("bob");
    scores.clear();

    CHRONICLE_CHECK(scores.empty());

    auto hx = chronicle::history(scores);
    CHRONICLE_CHECK(hx.size() == 5);
    CHRONICLE_CHECK(hx[0].value.kind == ContainerOpKind::Insert);
    CHRONICLE_CHECK(hx[1].value.kind == ContainerOpKind::Update);
    CHRONICLE_CHECK(hx[2].value.kind == ContainerOpKind::Insert);
    CHRONICLE_CHECK(hx[3].value.kind == ContainerOpKind::Erase);
    CHRONICLE_CHECK(hx[4].value.kind == ContainerOpKind::Clear);
}

CHRONICLE_TEST(snapshot_at_version_reconstructs_map_contents_via_replay) {
    Session session;
    chronicle::tracked_map<std::string, int> scores;
    chronicle::track(scores, session, "scores");

    scores.set("alice", 10);
    scores.set("bob", 20);
    auto const v0 = chronicle::current_version(scores);

    scores.set("alice", 15); // update
    scores.set("carol", 30); // insert
    auto const v1 = chronicle::current_version(scores);

    scores.erase("bob");

    auto snap0 = chronicle::snapshot_at_version(scores, v0);
    CHRONICLE_CHECK(snap0.has_value());
    CHRONICLE_CHECK(snap0->value.size() == 2);
    CHRONICLE_CHECK(snap0->value.at("alice") == 10);
    CHRONICLE_CHECK(snap0->value.at("bob") == 20);

    auto snap1 = chronicle::snapshot_at_version(scores, v1);
    CHRONICLE_CHECK(snap1.has_value());
    CHRONICLE_CHECK(snap1->value.size() == 3);
    CHRONICLE_CHECK(snap1->value.at("alice") == 15);
    CHRONICLE_CHECK(snap1->value.at("bob") == 20);
    CHRONICLE_CHECK(snap1->value.at("carol") == 30);

    // Live state reflects the later erase("bob") that happened after v1.
    CHRONICLE_CHECK(scores.size() == 2);
    CHRONICLE_CHECK(scores.find("bob") == nullptr);
}

CHRONICLE_TEST(map_diff_reports_inserts_updates_and_erases_by_key) {
    Session session;
    chronicle::tracked_map<std::string, int> scores;
    chronicle::track(scores, session, "scores");

    scores.set("alice", 10);
    scores.set("bob", 20);
    auto const v0 = chronicle::current_version(scores);

    scores.set("alice", 15);  // update
    scores.set("carol", 30);  // insert
    scores.erase("bob");      // erase
    auto const v1 = chronicle::current_version(scores);

    auto snap0 = chronicle::snapshot_at_version(scores, v0);
    auto snap1 = chronicle::snapshot_at_version(scores, v1);
    CHRONICLE_CHECK(snap0.has_value());
    CHRONICLE_CHECK(snap1.has_value());

    auto d = chronicle::diff(*snap0, *snap1);
    CHRONICLE_CHECK(d.changes.size() == 3);

    bool saw_update = false, saw_insert = false, saw_erase = false;
    for (auto const& change : d) {
        if (change.kind == ContainerOpKind::Update) {
            CHRONICLE_CHECK(change.key == "alice");
            CHRONICLE_CHECK(change.before == 10);
            CHRONICLE_CHECK(change.after == 15);
            saw_update = true;
        } else if (change.kind == ContainerOpKind::Insert) {
            CHRONICLE_CHECK(change.key == "carol");
            CHRONICLE_CHECK(change.after == 30);
            saw_insert = true;
        } else if (change.kind == ContainerOpKind::Erase) {
            CHRONICLE_CHECK(change.key == "bob");
            CHRONICLE_CHECK(change.before == 20);
            saw_erase = true;
        }
    }
    CHRONICLE_CHECK(saw_update);
    CHRONICLE_CHECK(saw_insert);
    CHRONICLE_CHECK(saw_erase);
}
