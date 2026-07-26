// docs/adr/0031-object-graph.md ("Layer 2"): chronicle::object_name_of()
// and the Session-level object_names()/field_names_of() queries.

#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

#include <algorithm>

using namespace chronicle;

CHRONICLE_TEST(object_name_of_splits_on_the_last_dot) {
    CHRONICLE_CHECK(object_name_of("player.health") == "player");
    CHRONICLE_CHECK(object_name_of("player.stats.mana") == "player.stats");
    CHRONICLE_CHECK(object_name_of("score") == "score"); // no dot at all
    CHRONICLE_CHECK(object_name_of("a.") == "a");        // trailing dot
}

CHRONICLE_TEST(object_names_groups_streams_by_prefix_in_first_seen_order) {
    Session session;
    tracked<int> health{100};
    tracked<int> mana{50};
    tracked<int> score{0};
    track(health, session, "player.health");
    track(mana, session, "player.mana");
    track(score, session, "match.score");

    auto const objects = object_names(session);
    CHRONICLE_CHECK(objects.size() == 2);
    CHRONICLE_CHECK(objects[0] == "player"); // first-seen order, not sorted
    CHRONICLE_CHECK(objects[1] == "match");
}

CHRONICLE_TEST(field_names_of_returns_only_that_objects_streams) {
    Session session;
    tracked<int> health{100};
    tracked<int> mana{50};
    tracked<int> score{0};
    track(health, session, "player.health");
    track(mana, session, "player.mana");
    track(score, session, "match.score");

    auto player_fields = field_names_of(session, "player");
    std::sort(player_fields.begin(), player_fields.end());
    CHRONICLE_CHECK(player_fields.size() == 2);
    CHRONICLE_CHECK(player_fields[0] == "player.health");
    CHRONICLE_CHECK(player_fields[1] == "player.mana");

    auto const match_fields = field_names_of(session, "match");
    CHRONICLE_CHECK(match_fields.size() == 1);
    CHRONICLE_CHECK(match_fields[0] == "match.score");
}

CHRONICLE_TEST(field_names_of_is_empty_for_an_object_with_no_fields) {
    Session session;
    tracked<int> health{100};
    track(health, session, "player.health");
    CHRONICLE_CHECK(field_names_of(session, "nonexistent").empty());
}

CHRONICLE_TEST(a_name_with_no_dot_is_its_own_single_field_object) {
    Session session;
    tracked<int> score{0};
    track(score, session, "score");
    auto const objects = object_names(session);
    CHRONICLE_CHECK(objects.size() == 1);
    CHRONICLE_CHECK(objects[0] == "score");
    CHRONICLE_CHECK(field_names_of(session, "score").size() == 1);
}
