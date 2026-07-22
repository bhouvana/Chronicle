// chronicle-adapter-entt (docs/adr/0015-entt-adapter.md). Only built when
// EnTT's CMake package is found (tests/unit/CMakeLists.txt) -- same
// opt-in reasoning as compression_test.cpp and tools/codegen.

#include "chronicle/adapters/entt.hpp"
#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

#include <entt/entt.hpp>

using namespace chronicle;
using namespace chronicle::adapters::entt;

namespace {

struct Position {
    float x = 0.0f;
    float y = 0.0f;
};

} // namespace

CHRONICLE_TRACK_TYPE(Position, x, y);

CHRONICLE_TEST(entt_adapter_records_insert_on_construct) {
    ::entt::registry registry;
    Session session;
    auto tracker = track_component<Position>(registry, session, "position");

    auto const e = registry.create();
    registry.emplace<Position>(e, 3.0f, 4.0f);

    auto const hx = tracker.stream<0>().history();
    CHRONICLE_CHECK(hx.size() == 1);
    CHRONICLE_CHECK(hx.front().value.kind == ContainerOpKind::Insert);
    CHRONICLE_CHECK(hx.front().value.key == ::entt::to_integral(e));
    CHRONICLE_CHECK(hx.front().value.value == 3.0f);
}

CHRONICLE_TEST(entt_adapter_records_update_on_patch) {
    ::entt::registry registry;
    Session session;
    auto tracker = track_component<Position>(registry, session, "position");

    auto const e = registry.create();
    registry.emplace<Position>(e, 3.0f, 4.0f);
    registry.patch<Position>(e, [](Position& p) { p.x = 9.0f; });

    auto const hx = tracker.stream<0>().history();
    CHRONICLE_CHECK(hx.size() == 2);
    CHRONICLE_CHECK(hx.back().value.kind == ContainerOpKind::Update);
    CHRONICLE_CHECK(hx.back().value.value == 9.0f);
}

CHRONICLE_TEST(entt_adapter_records_erase_on_remove_with_no_carried_value) {
    ::entt::registry registry;
    Session session;
    auto tracker = track_component<Position>(registry, session, "position");

    auto const e = registry.create();
    registry.emplace<Position>(e, 3.0f, 4.0f);
    registry.remove<Position>(e);

    auto const hx = tracker.stream<0>().history();
    CHRONICLE_CHECK(hx.size() == 2);
    CHRONICLE_CHECK(hx.back().value.kind == ContainerOpKind::Erase);
    CHRONICLE_CHECK(hx.back().value.value == 0.0f); // matches tracked_map<K,V>::erase()'s convention
}

CHRONICLE_TEST(entt_adapter_tracks_every_registered_field_independently) {
    ::entt::registry registry;
    Session session;
    auto tracker = track_component<Position>(registry, session, "position");

    auto const e = registry.create();
    registry.emplace<Position>(e, 1.0f, 2.0f);

    auto const hx_x = tracker.stream<0>().history();
    auto const hx_y = tracker.stream<1>().history();
    CHRONICLE_CHECK(hx_x.size() == 1);
    CHRONICLE_CHECK(hx_y.size() == 1);
    CHRONICLE_CHECK(hx_x.front().value.value == 1.0f);
    CHRONICLE_CHECK(hx_y.front().value.value == 2.0f);
}

CHRONICLE_TEST(entt_adapter_backfills_entities_that_predate_tracking) {
    ::entt::registry registry;
    Session session;

    auto const e = registry.create();
    registry.emplace<Position>(e, 7.0f, 8.0f); // no tracker attached yet

    auto tracker = track_component<Position>(registry, session, "position");

    auto const hx = tracker.stream<0>().history();
    CHRONICLE_CHECK(hx.size() == 1);
    CHRONICLE_CHECK(hx.front().value.kind == ContainerOpKind::Insert);
    CHRONICLE_CHECK(hx.front().value.key == ::entt::to_integral(e));
    CHRONICLE_CHECK(hx.front().value.value == 7.0f);
}

CHRONICLE_TEST(entt_adapter_keys_are_independent_across_multiple_entities) {
    ::entt::registry registry;
    Session session;
    auto tracker = track_component<Position>(registry, session, "position");

    auto const e1 = registry.create();
    registry.emplace<Position>(e1, 1.0f, 0.0f);
    auto const e2 = registry.create();
    registry.emplace<Position>(e2, 2.0f, 0.0f);

    auto const hx = tracker.stream<0>().history();
    CHRONICLE_CHECK(hx.size() == 2);
    CHRONICLE_CHECK(hx[0].value.key == ::entt::to_integral(e1));
    CHRONICLE_CHECK(hx[1].value.key == ::entt::to_integral(e2));
}
