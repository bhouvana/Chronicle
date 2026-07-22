#pragma once

#include <map>

#include "chronicle/container_op.hpp"

// Map counterpart to container_op.hpp: same ContainerOpKind vocabulary
// (docs/06-recording-model.md's "index-based insert/remove/update triples"
// generalized to "key-based" for associative containers), reused rather
// than duplicated -- Insert/Erase/Update/Clear mean the same thing whether
// the container is positional or keyed.

namespace chronicle {

template <typename K, typename V>
struct MapOp {
    ContainerOpKind kind = ContainerOpKind::Insert;
    K key{};
    V value{}; // unused for Erase/Clear

    // std::map<K, V>, not an unordered container: iteration order of a
    // replayed snapshot should be deterministic, matching this project's
    // general preference for reproducibility wherever it's cheap to have
    // (see docs/03's feasibility notes on determinism). Consequence: K must
    // be strictly-weakly-orderable (operator<), same requirement std::map
    // itself imposes -- tracked_map does not widen or narrow that.
};

template <typename K, typename V>
void apply(std::map<K, V>& target, MapOp<K, V> const& op) {
    switch (op.kind) {
        case ContainerOpKind::Insert:
        case ContainerOpKind::Update:
            target.insert_or_assign(op.key, op.value);
            break;
        case ContainerOpKind::Erase:
            target.erase(op.key);
            break;
        case ContainerOpKind::Clear:
            target.clear();
            break;
    }
}

} // namespace chronicle
