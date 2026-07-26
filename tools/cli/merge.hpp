#pragma once

#include <chronicle/io/loaded_session.hpp>

#include <ostream>
#include <string>
#include <utility>
#include <vector>

// docs/12-future-research-topics.md topic 6, docs/adr/0028-multiprocess-merge.md.
// "Everything in this document set assumes a single process" -- this does
// NOT lift that assumption. It combines multiple already-captured,
// independently-produced .chronicle files (one per process) into one
// viewable/queryable file, namespacing each process's streams by a caller-
// supplied tag so `player.health` from a client and `player.health` from a
// server don't collide. It deliberately does NOT attempt to establish any
// ordering *between* streams from different input files -- see merge.cpp
// for why that would require real clock synchronization this project
// hasn't built (docs/12 topic 6's own "materially different trust/security
// model" caveat), not just relabeling existing data.

namespace chronicle_cli {

// `tagged_inputs`: (process tag, already-loaded session) pairs. Each
// stream's on-disk name becomes "<tag>.<original-name>" in the merged
// output; within-stream (i.e. within-process) event order is preserved
// exactly as captured.
void write_merged_session(
    std::vector<std::pair<std::string, chronicle::io::LoadedSession>> const& tagged_inputs,
    std::ostream& out);

} // namespace chronicle_cli
