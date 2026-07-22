#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "chronicle/container_op.hpp"
#include "chronicle/snapshot.hpp"

// docs/06-recording-model.md: "element-wise container diffing... lands with
// the container adapters." This is that: a diff() overload specialized for
// Snapshot<std::vector<T>>, more specific than diff.hpp's generic
// Diff<T> diff(Snapshot<T>, Snapshot<T>) and preferred for it by C++'s
// partial ordering of function template overloads.
//
// v0.2 scope: a simple index-aligned comparison (position i in `before`
// vs position i in `after`), not a minimum-edit-distance / LCS-based diff.
// That's an intentional simplification, not an oversight -- a real diff
// algorithm is more accurate for containers that have shifted (an insert at
// the front looks like N updates here, not 1 insert), but index alignment is
// enough to answer "what changed" for the common case (updates in place,
// appends at the end) and costs nothing more than one linear pass. Revisit
// if usage shows the naive version misleads more than it helps.

namespace chronicle {

template <typename T>
struct ContainerChange {
    ContainerOpKind kind = ContainerOpKind::Update;
    std::size_t index = 0;
    T before{};
    T after{};
};

template <typename T>
struct ContainerDiff {
    std::vector<ContainerChange<T>> changes;

    [[nodiscard]] bool empty() const noexcept { return changes.empty(); }
    [[nodiscard]] auto begin() const noexcept { return changes.begin(); }
    [[nodiscard]] auto end() const noexcept { return changes.end(); }
};

template <typename T>
[[nodiscard]] ContainerDiff<T> diff(Snapshot<std::vector<T>> const& before,
                                     Snapshot<std::vector<T>> const& after) {
    ContainerDiff<T> result;
    auto const& a = before.value;
    auto const& b = after.value;
    std::size_t const common = std::min(a.size(), b.size());

    for (std::size_t i = 0; i < common; ++i) {
        if (a[i] != b[i]) {
            result.changes.push_back(ContainerChange<T>{ContainerOpKind::Update, i, a[i], b[i]});
        }
    }
    for (std::size_t i = common; i < b.size(); ++i) {
        result.changes.push_back(ContainerChange<T>{ContainerOpKind::Insert, i, T{}, b[i]});
    }
    for (std::size_t i = common; i < a.size(); ++i) {
        result.changes.push_back(ContainerChange<T>{ContainerOpKind::Erase, i, a[i], T{}});
    }
    return result;
}

} // namespace chronicle
