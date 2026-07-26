#pragma once

#include <chronicle/io/loaded_session.hpp>

#include <string>
#include <vector>

// docs/adr/0031-object-graph.md ("Layer 2"). File-based counterpart to
// chronicle::object_names()/field_names_of() (include/chronicle/object_graph.hpp),
// which only work on a live in-process Session. Reuses
// chronicle::object_name_of() directly (a plain std::string function with
// no Session dependency) rather than re-deriving the splitting rule a
// second time -- the browser viewer's JS (ADR 0016) is the one place this
// rule is unavoidably duplicated, since it runs in a browser, not C++.

namespace chronicle_cli {

struct ObjectGroup {
    std::string name;
    std::vector<chronicle::io::LoadedStream const*> fields;
};

// Groups every stream in `session` by chronicle::object_name_of(), in
// first-seen order (matching object_names()'s live-Session behavior).
[[nodiscard]] std::vector<ObjectGroup> group_by_object(chronicle::io::LoadedSession const& session);

// docs/adr/0034-object-snapshot.md ("Layer 5"). One entry from an
// object's merged, chronologically-ordered event log -- shared by
// `chronicle-cli object-history` (prints every entry) and
// `chronicle-cli object-snapshot` (folds entries up to one position),
// so the two subcommands can't drift apart on ordering.
struct MergedEntry {
    std::string field_name;
    chronicle::io::LoadedEvent const* event;
    chronicle::io::StreamShape shape;
};

struct MergedObjectHistory {
    std::vector<MergedEntry> entries;
    bool ordered_by_hlc = false; // false means best-effort elapsed_ns fallback -- see merge_object_history()
};

// Best-effort chronological merge across independent streams: prefers the
// HLC (ADR 0019's one real cross-stream-comparable ordinal) only when
// EVERY event in `group` has one; otherwise falls back to elapsed_ns.
// Never claims stronger cross-stream ordering than ADR 0003/0019 already
// established.
[[nodiscard]] MergedObjectHistory merge_object_history(ObjectGroup const& group);

} // namespace chronicle_cli
