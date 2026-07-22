#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <source_location>
#include <string>
#include <utility>

#include "chronicle/diff.hpp"
#include "chronicle/session.hpp"
#include "chronicle/snapshot.hpp"
#include "chronicle/stream.hpp"
#include "chronicle/timeline.hpp"

// docs/07-api-design.md's core surface: tracked<T>, track(), history(),
// snapshot(), diff(). "Zero-cost when untracked" (docs/07's first design
// principle) is realized here as: an untracked tracked<T> has stream_ ==
// nullptr, so operator= is a plain move-assign plus one predictable branch
// on a pointer that's null for the entire lifetime of an untracked field --
// see docs/09-performance.md for why this, not a runtime flag lookup, is the
// mechanism (compile-time-resolvable, not virtual-dispatch-based).

namespace chronicle {

template <typename T>
class tracked {
public:
    tracked() = default;
    explicit tracked(T initial) : value_(std::move(initial)) {}

    tracked(tracked const&) = delete;
    tracked& operator=(tracked const&) = delete;

    // Does NOT capture a call site: a non-static member operator= is
    // language-required to have exactly one parameter (confirmed by
    // compiler, not assumed -- see docs/adr/0010-call-site-capture.md), so
    // there is nowhere to put a defaulted std::source_location here. Passes
    // an explicit empty source_location (not record()'s own default) --
    // otherwise every plain assignment across the whole program would
    // misleadingly report the same fixed line inside this operator=, which
    // would be actively worse than reporting "unknown." Use chronicle::set()
    // below when the call site matters.
    tracked& operator=(T value) {
        value_ = std::move(value);
        if (stream_ != nullptr) {
            stream_->record(value_, std::source_location{});
        }
        return *this;
    }

    operator T const&() const noexcept { return value_; }
    [[nodiscard]] T const& get() const noexcept { return value_; }

    // Bound by track(); not intended to be called directly by user code.
    void bind(Stream<T>& stream) noexcept { stream_ = &stream; }
    [[nodiscard]] Stream<T>* stream() const noexcept { return stream_; }

    // Used by chronicle::set() below, which is the one place that has
    // already captured a real caller location to forward. Not exposed as
    // ergonomically as operator= on purpose -- this exists to serve set(),
    // not to be a second general-purpose assignment spelling.
    void assign(T value, std::source_location call_site) {
        value_ = std::move(value);
        if (stream_ != nullptr) {
            stream_->record(value_, call_site);
        }
    }

private:
    T value_{};
    Stream<T>* stream_ = nullptr;
};

// docs/07: `chronicle::track(player, session, "player_1")`.
// Records the field's current value as version 0, so history() always has at
// least one entry (the value as of the moment tracking began) rather than an
// empty timeline until the first subsequent mutation. track() is a plain
// function template (not an operator), so -- unlike tracked<T>::operator= --
// it can and does capture its own call site: "where tracking of this field
// began" is itself meaningful causal information.
template <typename T>
Stream<T>& track(tracked<T>& field, Session& session, std::string name,
                  std::source_location call_site = std::source_location::current()) {
    auto& stream = session.create_stream<T>(std::move(name));
    field.bind(stream);
    stream.record(field.get(), call_site);
    return stream;
}

// Explicit assignment with call-site capture -- the counterpart to plain
// `field = value` for callers who want "which line did this" answered
// (docs/10-roadmap.md's v0.5 causal-chain-queries item). Exists specifically
// because tracked<T>::operator= cannot capture this itself (see the comment
// on operator= above and docs/adr/0010-call-site-capture.md).
template <typename T>
void set(tracked<T>& field, T value,
         std::source_location call_site = std::source_location::current()) {
    field.assign(std::move(value), call_site);
}

template <typename T>
[[nodiscard]] Timeline<T> history(tracked<T> const& field) {
    if (auto* stream = field.stream()) {
        return stream->history();
    }
    return Timeline<T>{};
}

template <typename T>
[[nodiscard]] std::optional<Snapshot<T>> snapshot(
    tracked<T> const& field, std::chrono::steady_clock::time_point at) {
    if (auto* stream = field.stream()) {
        return stream->snapshot_at(at);
    }
    return std::nullopt;
}

// Exact counterpart to snapshot() above -- see Stream<T>::snapshot_at_version()
// for why this, not the time_point overload, is the one to prefer whenever
// the caller controls versions directly (e.g. via current_version()).
template <typename T>
[[nodiscard]] std::optional<Snapshot<T>> snapshot_at_version(tracked<T> const& field,
                                                               std::uint64_t version) {
    if (auto* stream = field.stream()) {
        return stream->snapshot_at_version(version);
    }
    return std::nullopt;
}

template <typename T>
[[nodiscard]] std::uint64_t current_version(tracked<T> const& field) {
    auto* stream = field.stream();
    return stream != nullptr ? stream->current_version() : 0;
}

// docs/10-roadmap.md's v0.5 "causal-chain queries ('what last wrote this
// value')" item: the full record of the most recent mutation, including
// where it happened (event.hpp's call_site) -- empty for events recorded
// via plain `field = value` assignment, since operator= structurally cannot
// capture a call site; populated for events recorded via chronicle::set()
// or the initial chronicle::track() call. See
// docs/adr/0010-call-site-capture.md.
template <typename T>
[[nodiscard]] std::optional<HistoryRecord<T>> last_writer(tracked<T> const& field) {
    auto* stream = field.stream();
    return stream != nullptr ? stream->last_writer() : std::nullopt;
}

// Explicit named snapshot point (docs/07: `chronicle::checkpoint(session,
// "level_loaded")`). v0.1 records it as an ordinary event on every stream in
// the session rather than a distinct marker type; the label is not yet
// persisted anywhere queryable -- resolving that is left to the on-disk
// session format work slated for v0.2 (docs/10-roadmap.md), so this exists
// today mainly to force a drain of every stream at a known point in time.
inline void checkpoint(Session& session, std::string const& /*label*/) {
    session.drain_all();
}

} // namespace chronicle
