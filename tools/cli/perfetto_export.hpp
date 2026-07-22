#pragma once

#include <chronicle/io/loaded_session.hpp>

#include <ostream>

// docs/adr/0020-perfetto-export-bridge.md (docs/10-roadmap.md's v2.0 item):
// `chronicle-cli export --perfetto` -- reuses Perfetto's mature timeline
// UI (docs/08-visualization.md's "Interop over reinvention" principle,
// already applied once for the Tracy bridge, docs/adr/0013) instead of
// Chronicle building a second competing timeline view.
//
// Emits the legacy Chrome JSON Trace Event Format (a bare JSON array of
// event objects), not Perfetto's native protobuf Trace format -- verified
// directly (not assumed) that ui.perfetto.dev loads Chrome JSON traces
// natively, and the JSON format needs no new dependency (this file reuses
// html_export.cpp's json_escape()-style helpers), unlike protobuf, which
// would need a real serialization library for a comparatively rare export
// path.

namespace chronicle_cli {

void write_perfetto_export(chronicle::io::LoadedSession const& session, std::ostream& out);

} // namespace chronicle_cli
