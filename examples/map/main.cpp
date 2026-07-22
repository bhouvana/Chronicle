// Demonstrates tracked_map<K,V>: structural-delta history keyed by K,
// replay-based version-exact snapshots, and key-based diff -- the v0.2 map
// adapter (docs/06-recording-model.md, docs/07-api-design.md).

#include <chronicle/chronicle.hpp>
#include <iostream>
#include <string>

int main() {
    chronicle::Session session;

    chronicle::tracked_map<std::string, int> scores;
    chronicle::track(scores, session, "match.scores");

    scores.set("alice", 10);
    scores.set("bob", 20);
    auto const v0 = chronicle::current_version(scores);

    scores.set("alice", 15); // existing key -> recorded as Update
    scores.set("carol", 30); // new key -> recorded as Insert
    auto const v1 = chronicle::current_version(scores);

    scores.erase("bob");

    std::cout << "raw op history:\n";
    for (auto const& record : chronicle::history(scores)) {
        std::cout << "  v" << record.version << ": ";
        switch (record.value.kind) {
            case chronicle::ContainerOpKind::Insert:
                std::cout << "insert[" << record.value.key << "] = " << record.value.value;
                break;
            case chronicle::ContainerOpKind::Erase:
                std::cout << "erase[" << record.value.key << "]";
                break;
            case chronicle::ContainerOpKind::Update:
                std::cout << "update[" << record.value.key << "] = " << record.value.value;
                break;
            case chronicle::ContainerOpKind::Clear:
                std::cout << "clear";
                break;
        }
        std::cout << "\n";
    }

    auto snap0 = chronicle::snapshot_at_version(scores, v0);
    auto snap1 = chronicle::snapshot_at_version(scores, v1);

    std::cout << "\ndiff between v" << v0 << " and v" << v1 << ":\n";
    if (snap0 && snap1) {
        for (auto const& change : chronicle::diff(*snap0, *snap1)) {
            switch (change.kind) {
                case chronicle::ContainerOpKind::Update:
                    std::cout << "  \"" << change.key << "\": " << change.before << " -> "
                              << change.after << "\n";
                    break;
                case chronicle::ContainerOpKind::Insert:
                    std::cout << "  \"" << change.key << "\": (new) -> " << change.after << "\n";
                    break;
                case chronicle::ContainerOpKind::Erase:
                    std::cout << "  \"" << change.key << "\": " << change.before << " -> (removed)\n";
                    break;
                default:
                    break;
            }
        }
    }

    std::cout << "\ncurrent contents:\n";
    for (auto const& [key, value] : scores) {
        std::cout << "  " << key << " = " << value << "\n";
    }

    return 0;
}
