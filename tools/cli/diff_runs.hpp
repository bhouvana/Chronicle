#pragma once

#include <chronicle/io/loaded_session.hpp>

#include <ostream>

// docs/12-future-research-topics.md topic 5, docs/adr/0025-cross-run-diffing.md:
// "diff two runs of the same scenario" for the simulation/robotics audience
// (docs/02-competitive-gap-analysis.md). Two independently-produced
// .chronicle files have no shared object identity/address (different
// process runs -- Phase 4's generation-counted handles are single-run
// scoped by construction), so streams are aligned by *name* instead, the
// semantic alignment key docs/12 calls for.

namespace chronicle_cli {

// Writes a plain-text report to `out`; returns true if any difference
// (missing stream or diverging value) was found, false if the two runs
// matched exactly on every commonly-named stream.
bool write_run_diff(chronicle::io::LoadedSession const& run_a, chronicle::io::LoadedSession const& run_b,
                     std::ostream& out);

} // namespace chronicle_cli
