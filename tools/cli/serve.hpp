#pragma once

#include <string>

// docs/adr/0016-interactive-browser-viewer.md: `chronicle-cli serve`, only
// built when cpp-httplib's CMake package is found (tools/cli/CMakeLists.txt)
// -- same opt-in reasoning as Zstd/Tracy/EnTT support elsewhere in this
// project. Declared unconditionally so main.cpp's dispatch table compiles
// either way; cmd_serve() itself only exists (serve.cpp) in the
// httplib-available build, and CHRONICLE_CLI_HAVE_HTTPLIB guards main.cpp's
// one call site.

namespace chronicle_cli {

int cmd_serve(std::string const& path, int port);

} // namespace chronicle_cli
