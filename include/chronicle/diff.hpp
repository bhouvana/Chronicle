#pragma once

#include "chronicle/snapshot.hpp"

// docs/07-api-design.md: "Diff<T>: field-by-field ... comparison of two
// Snapshot<T>s ... the primitive the visualization layer renders directly."
// v0.1 scope is scalar T only (see docs/rfc/0001), so a Diff<T> here is just
// a before/after pair with a changed flag -- element-wise container diffing
// (docs/06's structural delta model) lands with the container adapters.

namespace chronicle {

template <typename T>
struct Diff {
    T before{};
    T after{};
    bool changed = false;
};

template <typename T>
[[nodiscard]] Diff<T> diff(Snapshot<T> const& before, Snapshot<T> const& after) {
    return Diff<T>{before.value, after.value, before.value != after.value};
}

} // namespace chronicle
