// Produces demo.chronicle in the current directory: a scalar, a vector,
// and a map stream in one session file, for chronicle-cli
// (tools/cli/main.cpp) to read. Run this first, then e.g.:
//
//   chronicle-cli list demo.chronicle
//   chronicle-cli history demo.chronicle player.health
//   chronicle-cli diff demo.chronicle player.inventory 1 3

#include <chronicle/chronicle.hpp>
#include <chronicle/io/session_writer.hpp>

#include <fstream>
#include <iostream>
#include <string>

int main() {
    chronicle::Session session;

    chronicle::tracked<int> health{100};
    chronicle::track(health, session, "player.health");
    health = 75;
    health = 45;
    health = -5;

    chronicle::tracked_vector<std::string> inventory;
    chronicle::track(inventory, session, "player.inventory");
    inventory.push_back("sword");
    inventory.push_back("shield");
    inventory.push_back("potion");
    inventory.update(0, "enchanted sword");

    chronicle::tracked_map<std::string, int> scores;
    chronicle::track(scores, session, "match.scores");
    scores.set("alice", 10);
    scores.set("bob", 20);
    scores.set("alice", 15);

    std::ofstream out("demo.chronicle", std::ios::binary);
    if (!out) {
        std::cerr << "failed to open demo.chronicle for writing\n";
        return 1;
    }
    chronicle::io::SessionWriter writer(out, session);
    writer.write(health);
    writer.write(inventory);
    writer.write(scores);
    out.close();

    std::cout << "wrote demo.chronicle (3 streams)\n";
    return 0;
}
