#pragma once

#include <string>

// docs/adr/0042-json-output-modes.md: shared by objects/doctor/narrate's
// --json output. Unlike html_export.cpp/perfetto_export.cpp's own
// independently-duplicated json_escape() (deliberately self-contained
// exporters targeting genuinely different external systems), these three
// commands are one concept -- structured output for tooling/AI
// consumption -- so one shared helper, not three copies with no reason to
// diverge.

namespace chronicle_cli {

[[nodiscard]] std::string json_escape(std::string const& s);

} // namespace chronicle_cli
