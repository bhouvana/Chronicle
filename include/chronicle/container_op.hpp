#pragma once

#include <cstddef>
#include <vector>

// docs/06-recording-model.md: "Delta encoding for containers uses a simple
// structural diff (index-based insert/remove/update triples) rather than a
// generic byte-diff -- cheaper to compute and directly maps to
// push_back/erase/assignment operations already known to the Instrumentation
// layer." This is that structural-diff payload type.
//
// A ContainerOp<T> is deliberately reused as the `T` parameter of the
// existing scalar Stream<T> (see stream.hpp) rather than requiring a new
// storage engine: docs/09-performance.md's "resolved at compile time via
// templates" principle applies here too -- Stream<ContainerOp<T>> gets the
// same operation-log-per-stream machinery Stream<int> already has, for free.

namespace chronicle {

enum class ContainerOpKind {
    Insert,
    Erase,
    Update,
    Clear,
};

template <typename T>
struct ContainerOp {
    ContainerOpKind kind = ContainerOpKind::Insert;
    std::size_t index = 0; // unused for Clear
    T value{};              // unused for Erase/Clear
};

// Replays a single structural-diff event onto a materialized std::vector<T>.
// This is the Replay Engine primitive from docs/05-architecture.md, applied
// concretely to containers: reconstructing "what did this look like" is
// nothing more than folding every ContainerOp since the beginning (or since
// the nearest snapshot, once container snapshots exist -- see the note in
// tracked_vector.hpp about why v0.2 doesn't need that yet).
template <typename T>
void apply(std::vector<T>& target, ContainerOp<T> const& op) {
    switch (op.kind) {
        case ContainerOpKind::Insert:
            if (op.index >= target.size()) {
                target.push_back(op.value);
            } else {
                target.insert(target.begin() + static_cast<std::ptrdiff_t>(op.index), op.value);
            }
            break;
        case ContainerOpKind::Erase:
            if (op.index < target.size()) {
                target.erase(target.begin() + static_cast<std::ptrdiff_t>(op.index));
            }
            break;
        case ContainerOpKind::Update:
            if (op.index < target.size()) {
                target[op.index] = op.value;
            }
            break;
        case ContainerOpKind::Clear:
            target.clear();
            break;
    }
}

} // namespace chronicle
