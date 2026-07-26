#pragma once

#include <chronicle/io/loaded_session.hpp>

#include <ostream>
#include <string>

// docs/13-vision.md Layer 10 ("engineering memory"), docs/adr/0038-narrative-composer.md.
//
// Layer 10 describes an emergent capability -- a composed narrative
// ("Renderer stalled because Physics waited...") that falls out of
// Layers 2 (object graph) + 3 (provenance) + 9 (bridges) working
// together, not a mechanism of its own. Layers 3/4's registries are
// in-process only (ADR 0032/0033) and genuinely don't exist once a
// process exits, so a CLI narrator operating on a `.chronicle` file
// cannot honestly incorporate them. This composes only what's actually
// persisted to disk: object-snapshot's per-field values (ADR 0034),
// each field's real call site (ADR 0010, persisted in every file since
// format v2), and a real cross-thread-proximity pass mirroring
// `chronicle::possible_race()`'s exact logic (ADR 0023) applied to
// `LoadedEvent` data instead of live `HistoryRecord<T>`.

namespace chronicle_cli {

void write_narration(chronicle::io::LoadedSession const& session, std::string const& object_name,
                      std::size_t position, std::ostream& out);

} // namespace chronicle_cli
