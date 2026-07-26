#pragma once

#include <chronicle/io/loaded_session.hpp>

#include "object_graph.hpp"

#include <cstdint>
#include <string>
#include <vector>

// docs/13-vision.md Layer 7 ("live queries"), docs/adr/0035-live-queries.md:
// "which variable changed most," "show me all writes from thread N" --
// answered directly over an already-loaded .chronicle file, reusing
// MergedEntry/merge_object_history (object_graph.hpp, ADR 0031/0034)
// rather than a second merge implementation.

namespace chronicle_cli {

struct StreamActivity {
    std::string name;
    std::size_t event_count;
};

// Every stream in `session`, ranked by event count descending -- "which
// variable changed most." Ties keep the session's own stream order
// (stable sort), not an arbitrary one.
[[nodiscard]] std::vector<StreamActivity> most_changed_streams(chronicle::io::LoadedSession const& session);

// Distinct thread_hash values across every event in `session`, in
// first-seen order (streams in session order, events in per-stream
// order) -- gives a friendly, stable "thread 0, thread 1, ..." index over
// the otherwise-opaque raw thread_hash, e.g. for `query thread <index>`.
[[nodiscard]] std::vector<std::uint64_t> thread_index(chronicle::io::LoadedSession const& session);

// Every event across every stream whose thread_hash == `thread_hash`,
// chronologically merged the same way merge_object_history() merges one
// object's fields (best-effort: HLC when every matching event has one,
// elapsed_ns otherwise) -- "show me all writes from this thread."
[[nodiscard]] MergedObjectHistory events_from_thread(chronicle::io::LoadedSession const& session,
                                                      std::uint64_t thread_hash);

} // namespace chronicle_cli
