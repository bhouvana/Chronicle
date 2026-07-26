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

// docs/adr/0036-whole-program-rewind.md ("Layer 6, scoped"): the exact
// same merge, over EVERY stream in the session rather than one object's
// fields -- "rewind everything Chronicle actually instruments, honestly
// labeled as best-effort across threads," not literal whole-program
// memory-level determinism (which stays out of scope; see the ADR for
// why). Mechanically this is merge_object_history() given a synthetic
// group containing every stream, not a second merge algorithm.
[[nodiscard]] MergedObjectHistory merge_entire_session(chronicle::io::LoadedSession const& session);

// docs/adr/0034-object-snapshot.md / docs/adr/0038-narrative-composer.md:
// one field's reconstructed value at a cutoff position -- the data half
// of what `cmd_object_snapshot`/`cmd_program_snapshot` print, factored out
// here so `chronicle-cli narrate` (ADR 0038) can consume the same
// snapshot data without re-deriving the fold logic a third time.
struct FieldSnapshot {
    std::string field_name;
    chronicle::io::StreamShape shape;
    bool recorded = false; // false => "(not yet recorded)" as of this cutoff
    std::string rendered;  // pre-rendered display value, valid when recorded == true
};

// Folds `merged` up to `cutoff` (inclusive) and reconstructs every field
// in `fields`' value at that point -- scalar fields take the last value
// at or before cutoff; IndexedOp/KeyedOp fields replay
// (replay_indexed/replay_keyed, tools/cli/replay.hpp) up to the highest
// version that field reached at or before cutoff.
[[nodiscard]] std::vector<FieldSnapshot> snapshot_fields(
    std::vector<chronicle::io::LoadedStream const*> const& fields, MergedObjectHistory const& merged,
    std::size_t cutoff);

} // namespace chronicle_cli
