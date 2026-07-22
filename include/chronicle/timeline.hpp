#pragma once

#include <vector>
#include <cstddef>
#include "chronicle/event.hpp"

// docs/07-api-design.md: "Timeline<T>: an ordered, lazily-materialized view
// over one stream's events -- iterable, filterable, and directly usable with
// range adaptors." v0.1 materializes eagerly at query time (see Stream<T>::
// history() in stream.hpp); lazy materialization is a later optimization,
// not an API-shape change, so callers written against this type today do not
// need to change when that lands.

namespace chronicle {

template <typename T>
class Timeline {
public:
    using const_iterator = typename std::vector<HistoryRecord<T>>::const_iterator;

    Timeline() = default;
    explicit Timeline(std::vector<HistoryRecord<T>> records) : records_(std::move(records)) {}

    [[nodiscard]] const_iterator begin() const noexcept { return records_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return records_.end(); }
    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
    [[nodiscard]] bool empty() const noexcept { return records_.empty(); }

    [[nodiscard]] HistoryRecord<T> const& operator[](std::size_t index) const { return records_[index]; }
    [[nodiscard]] HistoryRecord<T> const& back() const { return records_.back(); }
    [[nodiscard]] HistoryRecord<T> const& front() const { return records_.front(); }

private:
    std::vector<HistoryRecord<T>> records_;
};

} // namespace chronicle
