// The example from README.md and docs/07-api-design.md, made to compile and
// run: track a scalar field, mutate it, print its history.

#include <chronicle/chronicle.hpp>
#include <iostream>

struct Player {
    chronicle::tracked<int> health{100};
};

int main() {
    chronicle::Session session;

    Player player;
    chronicle::track(player.health, session, "player_1.health");

    player.health = player.health - 25;
    player.health = player.health - 30;
    player.health = player.health - 50;

    std::cout << "history of player_1.health:\n";
    for (auto const& record : chronicle::history(player.health)) {
        std::cout << "  v" << record.version << " = " << record.value << "\n";
    }

    return 0;
}
