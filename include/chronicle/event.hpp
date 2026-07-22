#pragma once

#include <chrono>
#include <cstdint>
#include <source_location>
#include <thread>

#include "chronicle/hlc.hpp"

// Event & history record types.
// See docs/06-recording-model.md: an event is {version, timestamp, thread_id,
// causal_predecessor, payload}. For v0.1 scalar tracked<T>, payload is simply
// the new value (docs/06: "Payload is either the new value (scalar fields...)").
//
// call_site (docs/07-api-design.md's `rec.call_site` sketch, v0.5's "causal-
// chain queries" roadmap item): a default-constructed std::source_location
// (file_name() == "", line() == 0) means "not captured for this event," not
// "captured but empty" -- see docs/adr/0010-call-site-capture.md for why
// plain `field = value` assignment can never populate this (a hard language
// constraint on operator=, not an oversight) while named methods
// (push_back(), set(), chronicle::set()) can and do.
//
// hlc (docs/adr/0019-hybrid-logical-clock.md, v2.0): a default-constructed
// HlcTimestamp (all-zero) means "not computed for this event," same
// sentinel convention as call_site -- populated only when the owning
// Session's Config::causal_clock is true.

namespace chronicle {

template <typename T>
struct Event {
    T value{};
    std::uint64_t version = 0;
    std::chrono::steady_clock::time_point timestamp{};
    std::thread::id thread_id{};
    std::source_location call_site{};
    HlcTimestamp hlc{};
};

// What a history() query yields per entry. Identical shape to Event<T> today;
// kept as a distinct type because the query-facing record is allowed to grow
// fields that the internal Event representation may encode more compactly
// later (see docs/06's columnar-encoding note).
template <typename T>
struct HistoryRecord {
    T value{};
    std::uint64_t version = 0;
    std::chrono::steady_clock::time_point timestamp{};
    std::thread::id thread_id{};
    std::source_location call_site{};
    HlcTimestamp hlc{};
};

template <typename T>
[[nodiscard]] HistoryRecord<T> to_history_record(Event<T> const& event) {
    return HistoryRecord<T>{event.value,     event.version, event.timestamp,
                             event.thread_id, event.call_site, event.hlc};
}

// std::source_location has no "was this actually captured" query of its own
// (line() == 0 for a default-constructed one is the closest thing, but
// spelling that out at every call site would bury the actual meaning) --
// this is the one place that mapping is defined, so every caller reads the
// same thing chronicle/io/format.hpp's CallSiteInfo::known() means on the
// serialized side.
[[nodiscard]] inline bool is_known(std::source_location const& call_site) noexcept {
    return call_site.line() != 0;
}

} // namespace chronicle
