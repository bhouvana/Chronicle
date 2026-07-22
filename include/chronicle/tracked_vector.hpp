#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <source_location>
#include <string>
#include <utility>
#include <vector>

#include "chronicle/container_op.hpp"
#include "chronicle/session.hpp"
#include "chronicle/snapshot.hpp"
#include "chronicle/stream.hpp"
#include "chronicle/timeline.hpp"

// docs/07-api-design.md sketches `chronicle::tracked_vector<Item>` as one of
// the built-in adapters realizing the `Trackable` concept family. v0.2 scope
// per docs/10-roadmap.md: structural delta encoding (docs/06), no on-disk
// format yet, no tracked_map yet.
//
// Deliberately NO non-const operator[] / non-const iterators: those would
// hand out a `T&` whose mutation bypasses recording entirely, silently --
// exactly the "aliased raw pointer" class of blind spot docs/04-technical-
// limitations.md documents for tracked<T> fields. Rather than offer an API
// that quietly breaks its own guarantee, tracked_vector only mutates through
// push_back()/erase()/update()/clear(), each of which records what it did.
// This mirrors the "registration without wrapper types" explicit-setter
// pattern from docs/07 (`chronicle::set(p.seq, 42)`), applied to containers.

namespace chronicle {

template <typename T>
class tracked_vector {
public:
    tracked_vector() = default;

    tracked_vector(tracked_vector const&) = delete;
    tracked_vector& operator=(tracked_vector const&) = delete;

    // Each of these is a plain named method, not an operator, so -- unlike
    // tracked<T>::operator= -- it can capture its own call site directly
    // (docs/adr/0010-call-site-capture.md). Each explicitly forwards its own
    // captured call_site to record() rather than relying on record()'s own
    // default, which would otherwise report a fixed line inside this file
    // instead of the caller's.
    void push_back(T value, std::source_location call_site = std::source_location::current()) {
        std::size_t const index = data_.size();
        if (stream_ != nullptr) {
            stream_->record(ContainerOp<T>{ContainerOpKind::Insert, index, value}, call_site);
        }
        data_.push_back(std::move(value));
    }

    void erase(std::size_t index, std::source_location call_site = std::source_location::current()) {
        if (index >= data_.size()) {
            return;
        }
        if (stream_ != nullptr) {
            stream_->record(ContainerOp<T>{ContainerOpKind::Erase, index, T{}}, call_site);
        }
        data_.erase(data_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void update(std::size_t index, T value,
                std::source_location call_site = std::source_location::current()) {
        if (index >= data_.size()) {
            return;
        }
        if (stream_ != nullptr) {
            stream_->record(ContainerOp<T>{ContainerOpKind::Update, index, value}, call_site);
        }
        data_[index] = std::move(value);
    }

    void clear(std::source_location call_site = std::source_location::current()) {
        if (stream_ != nullptr && !data_.empty()) {
            stream_->record(ContainerOp<T>{ContainerOpKind::Clear, 0, T{}}, call_site);
        }
        data_.clear();
    }

    [[nodiscard]] T const& operator[](std::size_t index) const { return data_[index]; }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    [[nodiscard]] typename std::vector<T>::const_iterator begin() const noexcept { return data_.begin(); }
    [[nodiscard]] typename std::vector<T>::const_iterator end() const noexcept { return data_.end(); }

    void bind(Stream<ContainerOp<T>>& stream) noexcept { stream_ = &stream; }
    [[nodiscard]] Stream<ContainerOp<T>>* stream() const noexcept { return stream_; }

private:
    std::vector<T> data_;
    Stream<ContainerOp<T>>* stream_ = nullptr;
};

// Records the container's current contents as a sequence of Insert ops at
// tracking start, so history()/snapshot() are correct even for elements
// added before track() was called -- same rationale as scalar track()
// recording version 0 (see tracked.hpp).
template <typename T>
Stream<ContainerOp<T>>& track(tracked_vector<T>& field, Session& session, std::string name,
                               std::source_location call_site = std::source_location::current()) {
    auto& stream = session.create_stream<ContainerOp<T>>(std::move(name));
    field.bind(stream);
    for (std::size_t i = 0; i < field.size(); ++i) {
        stream.record(ContainerOp<T>{ContainerOpKind::Insert, i, field[i]}, call_site);
    }
    return stream;
}

template <typename T>
[[nodiscard]] Timeline<ContainerOp<T>> history(tracked_vector<T> const& field) {
    if (auto* stream = field.stream()) {
        return stream->history();
    }
    return Timeline<ContainerOp<T>>{};
}

namespace detail {

// Shared replay loop for both the timestamp- and version-bounded overloads
// below -- see docs/adr/0007-timestamp-ties-under-optimization.md for why
// there are two.
template <typename T, typename StopPredicate>
[[nodiscard]] std::optional<Snapshot<std::vector<T>>> replay_until(
    Stream<ContainerOp<T>>& stream, StopPredicate&& stop) {
    std::vector<T> materialized;
    std::uint64_t version = 0;
    std::chrono::steady_clock::time_point timestamp{};
    bool any = false;

    for (auto const& record : stream.history()) {
        if (stop(record)) {
            break;
        }
        apply(materialized, record.value);
        version = record.version;
        timestamp = record.timestamp;
        any = true;
    }

    if (!any) {
        return std::nullopt;
    }
    return Snapshot<std::vector<T>>{std::move(materialized), version, timestamp};
}

} // namespace detail

// Best-effort: see the tie-breaking caveat on Stream<T>::snapshot_at() in
// stream.hpp, which applies identically here. Prefer snapshot_at_version()
// below when the caller controls versions directly.
template <typename T>
[[nodiscard]] std::optional<Snapshot<std::vector<T>>> snapshot(
    tracked_vector<T> const& field, std::chrono::steady_clock::time_point at) {
    auto* stream = field.stream();
    if (stream == nullptr) {
        return std::nullopt;
    }
    return detail::replay_until<T>(
        *stream, [at](auto const& record) { return record.timestamp > at; });
}

// Exact counterpart to snapshot() above -- see Stream<T>::snapshot_at_version().
template <typename T>
[[nodiscard]] std::optional<Snapshot<std::vector<T>>> snapshot_at_version(
    tracked_vector<T> const& field, std::uint64_t version) {
    auto* stream = field.stream();
    if (stream == nullptr) {
        return std::nullopt;
    }
    return detail::replay_until<T>(
        *stream, [version](auto const& record) { return record.version > version; });
}

template <typename T>
[[nodiscard]] std::uint64_t current_version(tracked_vector<T> const& field) {
    auto* stream = field.stream();
    return stream != nullptr ? stream->current_version() : 0;
}

// docs/10-roadmap.md's v0.5 causal-chain-queries item: the most recent
// structural-delta op recorded for this container, including where it
// happened (see event.hpp's call_site).
template <typename T>
[[nodiscard]] std::optional<HistoryRecord<ContainerOp<T>>> last_writer(
    tracked_vector<T> const& field) {
    auto* stream = field.stream();
    return stream != nullptr ? stream->last_writer() : std::nullopt;
}

} // namespace chronicle
