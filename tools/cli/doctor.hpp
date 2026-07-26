#pragma once

#include <chronicle/io/loaded_session.hpp>

#include "object_graph.hpp"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

// docs/adr/0041-doctor-and-rules.md: "people love tools that tell them
// what's wrong before they know what to ask." chronicle-cli doctor is
// pure composition -- zero new detection algorithms beyond what
// narrate.cpp already established (a whole-file-scoped sibling of its
// position-bounded race/growth passes, not shared code with it: the two
// have genuinely different bounding semantics -- doctor sweeps an entire
// file once, narrate looks backward from one position -- so they stay
// separate, small implementations rather than a forced shared abstraction,
// same tolerance for at-the-boundary duplication this project already
// accepted for replay_indexed/replay_keyed vs. the typed apply()).

namespace chronicle_cli {

struct RaceFinding {
    std::string field_a;
    std::size_t position_a;
    std::string field_b;
    std::size_t position_b;
};

// Whole-file race scan: entries are already sorted (by HLC when every
// event has one, per merge_object_history()'s own rule), so this only
// ever compares each event against the next `max_lookahead` in sequence
// rather than every pair -- bounded, not quadratic, same reasoning
// narrate.cpp's position-bounded window already relies on.
[[nodiscard]] std::vector<RaceFinding> detect_races(MergedObjectHistory const& merged,
                                                     std::uint64_t window_us = 1000,
                                                     std::size_t max_lookahead = 20);

struct GrowthFinding {
    std::string field_name;
    std::size_t final_size;
};

// Whole-field growth scan (no version cutoff, unlike narrate's
// position-bounded check) -- flags a container that never shrank across
// its entire recorded history.
[[nodiscard]] std::vector<GrowthFinding> detect_growth_anomalies(
    std::vector<chronicle::io::LoadedStream const*> const& fields, std::size_t min_size_to_flag = 5);

// Runs every check above plus the existing object_graph.cpp/query.cpp
// primitives (most_changed_streams, thread_index) and prints one composed
// report. Returns true if any issue was found (races, growth anomalies,
// or streams with zero recorded events) -- the CLI maps this to a
// non-zero exit code, so `chronicle doctor` is usable as a CI health gate,
// not just an interactive report.
bool write_doctor_report(chronicle::io::LoadedSession const& session, std::ostream& out);

} // namespace chronicle_cli
