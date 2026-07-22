#pragma once

#include <chrono>
#include <cstdint>

// docs/07-api-design.md: "Snapshot<T>: a fully-materialized value of T ...
// as of a specific version/time -- the thing you'd actually serialize/compare."

namespace chronicle {

template <typename T>
struct Snapshot {
    T value{};
    std::uint64_t version = 0;
    std::chrono::steady_clock::time_point timestamp{};
};

} // namespace chronicle
