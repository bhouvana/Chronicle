// Demonstrates tracked_vector<T>: structural-delta history, replay-based
// snapshots, and element-wise diff -- the v0.2 container adapter
// (docs/06-recording-model.md, docs/07-api-design.md).

#include <chronicle/chronicle.hpp>
#include <iostream>
#include <string>

int main() {
    chronicle::Session session;

    chronicle::tracked_vector<std::string> inventory;
    chronicle::track(inventory, session, "player_1.inventory");

    inventory.push_back("sword");
    inventory.push_back("shield");
    auto const v0 = chronicle::current_version(inventory);

    inventory.push_back("potion");
    inventory.update(0, "enchanted sword");
    auto const v1 = chronicle::current_version(inventory);

    inventory.erase(1); // drop the shield

    std::cout << "raw op history:\n";
    for (auto const& record : chronicle::history(inventory)) {
        std::cout << "  v" << record.version << ": ";
        switch (record.value.kind) {
            case chronicle::ContainerOpKind::Insert:
                std::cout << "insert[" << record.value.index << "] = " << record.value.value;
                break;
            case chronicle::ContainerOpKind::Erase:
                std::cout << "erase[" << record.value.index << "]";
                break;
            case chronicle::ContainerOpKind::Update:
                std::cout << "update[" << record.value.index << "] = " << record.value.value;
                break;
            case chronicle::ContainerOpKind::Clear:
                std::cout << "clear";
                break;
        }
        std::cout << "\n";
    }

    // Version-bounded, not timestamp-bounded -- see
    // docs/adr/0007-timestamp-ties-under-optimization.md for why this is the
    // reliable choice whenever the caller controls versions.
    auto snap0 = chronicle::snapshot_at_version(inventory, v0);
    auto snap1 = chronicle::snapshot_at_version(inventory, v1);

    std::cout << "\ndiff between v" << v0 << " and v" << v1 << ":\n";
    if (snap0 && snap1) {
        for (auto const& change : chronicle::diff(*snap0, *snap1)) {
            std::cout << "  index " << change.index << ": \"" << change.before << "\" -> \""
                      << change.after << "\"\n";
        }
    }

    std::cout << "\ncurrent contents: ";
    for (auto const& item : inventory) {
        std::cout << item << " ";
    }
    std::cout << "\n";

    return 0;
}
