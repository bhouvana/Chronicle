#pragma once

#include <map>
#include <vector>

#include "chronicle/map_op.hpp"
#include "chronicle/snapshot.hpp"

// Map counterpart to container_diff.hpp. Unlike the vector diff (which is
// deliberately naive/index-aligned -- see the comment there), a key-based
// diff has no alignment ambiguity to begin with: a key either exists in
// both snapshots (compare values), only in `after` (Insert), or only in
// `before` (Erase). No approximation involved here.

namespace chronicle {

template <typename K, typename V>
struct MapChange {
    ContainerOpKind kind = ContainerOpKind::Update;
    K key{};
    V before{};
    V after{};
};

template <typename K, typename V>
struct MapDiff {
    std::vector<MapChange<K, V>> changes;

    [[nodiscard]] bool empty() const noexcept { return changes.empty(); }
    [[nodiscard]] auto begin() const noexcept { return changes.begin(); }
    [[nodiscard]] auto end() const noexcept { return changes.end(); }
};

template <typename K, typename V>
[[nodiscard]] MapDiff<K, V> diff(Snapshot<std::map<K, V>> const& before,
                                  Snapshot<std::map<K, V>> const& after) {
    MapDiff<K, V> result;
    auto const& a = before.value;
    auto const& b = after.value;

    for (auto const& [key, value] : a) {
        auto it = b.find(key);
        if (it == b.end()) {
            result.changes.push_back(MapChange<K, V>{ContainerOpKind::Erase, key, value, V{}});
        } else if (!(it->second == value)) {
            result.changes.push_back(MapChange<K, V>{ContainerOpKind::Update, key, value, it->second});
        }
    }
    for (auto const& [key, value] : b) {
        if (a.find(key) == a.end()) {
            result.changes.push_back(MapChange<K, V>{ContainerOpKind::Insert, key, V{}, value});
        }
    }
    return result;
}

} // namespace chronicle
