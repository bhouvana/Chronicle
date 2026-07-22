// docs/08-visualization.md's Tracy bridge (v0.5): attach live Tracy
// plotting to a tracked field's Stream and mutate it repeatedly, with a
// short delay between mutations so an external `tracy-capture` process (or
// the Tracy GUI profiler) has a real window to connect and observe live
// plot data points -- see docs/adr/0013-tracy-bridge.md for how this was
// verified headlessly (tracy-capture + tracy-csvexport, no GUI needed).

#include <chronicle/chronicle.hpp>
#include <chronicle/tracy_bridge.hpp>

#include <chrono>
#include <cstdio>
#include <thread>

struct Player {
    chronicle::tracked<int> health{100};
};

int main() {
    chronicle::Session session;

    Player player;
    auto& stream = chronicle::track(player.health, session, "player_1.health");
    auto plot_handle = chronicle::tracy_bridge::plot(stream, "player_1.health");

    std::printf("chronicle-example-tracy: recording, connect a Tracy client now...\n");
    std::fflush(stdout);

    for (int i = 0; i < 40; ++i) {
        player.health = player.health - (i % 2 == 0 ? 3 : -1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::printf("chronicle-example-tracy: done, final health = %d\n", static_cast<int>(player.health));
    return 0;
}
