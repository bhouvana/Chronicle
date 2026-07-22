#pragma once

#include <chronicle/io/loaded_session.hpp>

#include <ostream>
#include <string>

// docs/08-visualization.md tier 2: "a single self-contained HTML file
// (inline JS/CSS, no external fetch) rendering a timeline/scrubber for a
// session, shareable as a build artifact... No server, no install." This is
// the last v0.2 roadmap item (docs/10-roadmap.md).
//
// The exported page embeds the LoadedSession as inline JSON and replays
// IndexedOp/KeyedOp streams in JavaScript using the same op semantics as
// chronicle-cli's own replay_indexed()/replay_keyed() (main.cpp) -- this is
// a deliberate duplication (C++ for the CLI's text output, JS for the
// standalone page) rather than a shared implementation, because the page
// must have zero runtime dependency on anything but the browser itself.

namespace chronicle_cli {

void write_html_export(chronicle::io::LoadedSession const& session, std::ostream& out);

// docs/adr/0016-interactive-browser-viewer.md: the page `chronicle-cli
// serve` (tools/cli/serve.cpp) hands back for `GET /`. Same scrubber/event
// log as write_html_export's static page, plus a new "Objects" sidebar
// panel (object/ownership graph view, docs/10-roadmap.md's v1.0 item) and a
// Refresh button -- serve.cpp re-reads the session file fresh on every
// request, so Refresh (re-fetching /api/session and re-rendering in place)
// is how a still-running producer's periodic re-exports show up without a
// full page reload. Shares viewer_script() with the static page verbatim.
void write_serve_page(chronicle::io::LoadedSession const& session, std::ostream& out);

// Exposed for serve.cpp (docs/adr/0016-interactive-browser-viewer.md): the
// tier-3 interactive viewer's /api/session endpoint serves exactly this
// same JSON shape, fetched by JS instead of inlined into the page --
// reusing this function is what lets both tiers share one JS renderer
// (kSharedViewerScript, also below) instead of duplicating the replay
// logic a third time.
[[nodiscard]] std::string session_to_json(chronicle::io::LoadedSession const& session);

// The JS also shared between the static export and the live server: stream
// list rendering, replayIndexed/replayKeyed, the scrubber, and (serve-only
// additions live in serve.cpp's own small wrapper around this) the object/
// ownership graph view. `dataExpr` is the JS expression the script uses to
// obtain its DATA -- an inline literal for the static export, a fetch()
// call for the live server -- everything else is identical.
[[nodiscard]] std::string viewer_script(std::string const& data_expr);

} // namespace chronicle_cli
