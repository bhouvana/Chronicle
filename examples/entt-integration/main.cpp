// docs/adr/0015-entt-adapter.md: tracks a Position component's fields
// across a real entt::registry's construct/update/destroy lifecycle, with
// zero manual chronicle::track() calls at any of Chronicle's own mutation
// sites -- entt::registry::emplace/patch/remove drive everything.

#include <chronicle/adapters/entt.hpp>
#include <chronicle/chronicle.hpp>

#include <entt/entt.hpp>

#include <cstdio>

struct Position {
    float x = 0.0f;
    float y = 0.0f;
};

CHRONICLE_TRACK_TYPE(Position, x, y);

int main() {
    entt::registry registry;
    chronicle::Session session;

    auto tracker = chronicle::adapters::entt::track_component<Position>(registry, session, "position");

    auto const e1 = registry.create();
    registry.emplace<Position>(e1, 1.0f, 2.0f); // -> Insert

    auto const e2 = registry.create();
    registry.emplace<Position>(e2, 10.0f, 20.0f); // -> Insert

    registry.patch<Position>(e1, [](Position& p) { p.x = 5.0f; }); // -> Update
    registry.remove<Position>(e2);                                 // -> Erase

    auto& x_stream = tracker.stream<0>(); // Position's field 0 is `x` (CHRONICLE_TRACK_TYPE order)

    auto const hx = x_stream.history();
    std::printf("position.x history (%zu events):\n", hx.size());
    for (auto const& record : hx) {
        char const* kind = "?";
        switch (record.value.kind) {
            case chronicle::ContainerOpKind::Insert: kind = "insert"; break;
            case chronicle::ContainerOpKind::Update: kind = "update"; break;
            case chronicle::ContainerOpKind::Erase: kind = "erase"; break;
            case chronicle::ContainerOpKind::Clear: kind = "clear"; break;
        }
        std::printf("  v%llu entity=%u %s x=%f\n", static_cast<unsigned long long>(record.version),
                    record.value.key, kind, record.value.value);
    }

    return 0;
}
