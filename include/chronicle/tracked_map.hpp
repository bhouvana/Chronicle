#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <source_location>
#include <string>
#include <utility>

#include "chronicle/map_op.hpp"
#include "chronicle/session.hpp"
#include "chronicle/snapshot.hpp"
#include "chronicle/stream.hpp"
#include "chronicle/timeline.hpp"

// Map counterpart to tracked_vector.hpp -- same rationale throughout
// (structural delta payload reusing Stream<T>, no non-const access that
// would silently bypass recording, lives in include/chronicle/ per
// ADR 0006 since it has no external dependency).
//
// One API difference from tracked_vector: a single set(key, value) replaces
// separate insert()/update() calls. Unlike a vector, where "insert" and
// "update" are different operations at different call sites (you insert at
// an index you choose vs. update an index that already holds something),
// a map's natural verb is "this key now has this value" regardless of
// whether the key existed before -- set() determines Insert vs. Update from
// current state and records the right one, so the caller doesn't have to.

namespace chronicle {

template <typename K, typename V>
class tracked_map {
public:
    tracked_map() = default;

    tracked_map(tracked_map const&) = delete;
    tracked_map& operator=(tracked_map const&) = delete;

    // Plain named methods, not operators -- can and do capture their own
    // call site directly, forwarded explicitly to record() rather than
    // relying on its default (docs/adr/0010-call-site-capture.md).
    void set(K key, V value, std::source_location call_site = std::source_location::current()) {
        bool const existed = data_.find(key) != data_.end();
        if (stream_ != nullptr) {
            stream_->record(MapOp<K, V>{existed ? ContainerOpKind::Update : ContainerOpKind::Insert,
                                         key, value},
                             call_site);
        }
        data_.insert_or_assign(std::move(key), std::move(value));
    }

    void erase(K const& key, std::source_location call_site = std::source_location::current()) {
        if (data_.find(key) == data_.end()) {
            return;
        }
        if (stream_ != nullptr) {
            stream_->record(MapOp<K, V>{ContainerOpKind::Erase, key, V{}}, call_site);
        }
        data_.erase(key);
    }

    void clear(std::source_location call_site = std::source_location::current()) {
        if (stream_ != nullptr && !data_.empty()) {
            stream_->record(MapOp<K, V>{ContainerOpKind::Clear, K{}, V{}}, call_site);
        }
        data_.clear();
    }

    [[nodiscard]] V const* find(K const& key) const {
        auto it = data_.find(key);
        return it == data_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    [[nodiscard]] typename std::map<K, V>::const_iterator begin() const noexcept { return data_.begin(); }
    [[nodiscard]] typename std::map<K, V>::const_iterator end() const noexcept { return data_.end(); }

    void bind(Stream<MapOp<K, V>>& stream) noexcept { stream_ = &stream; }
    [[nodiscard]] Stream<MapOp<K, V>>* stream() const noexcept { return stream_; }

private:
    std::map<K, V> data_;
    Stream<MapOp<K, V>>* stream_ = nullptr;
};

template <typename K, typename V>
Stream<MapOp<K, V>>& track(tracked_map<K, V>& field, Session& session, std::string name,
                            std::source_location call_site = std::source_location::current()) {
    auto& stream = session.create_stream<MapOp<K, V>>(std::move(name));
    field.bind(stream);
    for (auto const& [key, value] : field) {
        stream.record(MapOp<K, V>{ContainerOpKind::Insert, key, value}, call_site);
    }
    return stream;
}

template <typename K, typename V>
[[nodiscard]] Timeline<MapOp<K, V>> history(tracked_map<K, V> const& field) {
    if (auto* stream = field.stream()) {
        return stream->history();
    }
    return Timeline<MapOp<K, V>>{};
}

namespace detail {

template <typename K, typename V, typename StopPredicate>
[[nodiscard]] std::optional<Snapshot<std::map<K, V>>> replay_map_until(
    Stream<MapOp<K, V>>& stream, StopPredicate&& stop) {
    std::map<K, V> materialized;
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
    return Snapshot<std::map<K, V>>{std::move(materialized), version, timestamp};
}

} // namespace detail

// Best-effort -- see the tie-breaking caveat on Stream<T>::snapshot_at() in
// stream.hpp and docs/adr/0007-timestamp-ties-under-optimization.md. Prefer
// snapshot_at_version() below when the caller controls versions directly.
template <typename K, typename V>
[[nodiscard]] std::optional<Snapshot<std::map<K, V>>> snapshot(
    tracked_map<K, V> const& field, std::chrono::steady_clock::time_point at) {
    auto* stream = field.stream();
    if (stream == nullptr) {
        return std::nullopt;
    }
    return detail::replay_map_until<K, V>(
        *stream, [at](auto const& record) { return record.timestamp > at; });
}

template <typename K, typename V>
[[nodiscard]] std::optional<Snapshot<std::map<K, V>>> snapshot_at_version(
    tracked_map<K, V> const& field, std::uint64_t version) {
    auto* stream = field.stream();
    if (stream == nullptr) {
        return std::nullopt;
    }
    return detail::replay_map_until<K, V>(
        *stream, [version](auto const& record) { return record.version > version; });
}

template <typename K, typename V>
[[nodiscard]] std::uint64_t current_version(tracked_map<K, V> const& field) {
    auto* stream = field.stream();
    return stream != nullptr ? stream->current_version() : 0;
}

// docs/10-roadmap.md's v0.5 causal-chain-queries item -- see the tracked_vector
// overload's comment in tracked_vector.hpp for the full rationale.
template <typename K, typename V>
[[nodiscard]] std::optional<HistoryRecord<MapOp<K, V>>> last_writer(
    tracked_map<K, V> const& field) {
    auto* stream = field.stream();
    return stream != nullptr ? stream->last_writer() : std::nullopt;
}

} // namespace chronicle
