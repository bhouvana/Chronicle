#include "serve.hpp"

#include "html_export.hpp"
#include "session_loader.hpp"

#include <httplib.h>

#include <iostream>
#include <sstream>

// docs/adr/0016-interactive-browser-viewer.md: the v1.0 "interactive
// browser viewer" item. Deliberately serves an *already-recorded*
// `.chronicle` file, re-read fresh on every request -- not a live
// connection to a still-running producer process. That would need a real
// socket transport (docs/06-recording-model.md's "stream live... over a
// local socket") this codebase doesn't have yet, the same gap
// docs/adr/0014-storage-engine-compression.md already found and deferred
// LZ4 live-stream compression on. Re-reading the file per request is the
// honest, low-tech approximation available today: point a teammate at a
// URL instead of a shared file, and hit Refresh (html_export.cpp's
// refreshData()) after the producer writes a new export to see it, without
// re-running `chronicle-cli export --html` and re-sharing a new file each
// time.

namespace chronicle_cli {

int cmd_serve(std::string const& path, int port) {
    // Fail fast on an unreadable/corrupt file before starting the server --
    // the same failure this tool's other subcommands surface immediately,
    // not deferred until the first browser request.
    [[maybe_unused]] auto const startup_check = load_file(path);

    httplib::Server server;

    server.Get("/", [&path](httplib::Request const&, httplib::Response& res) {
        auto const session = load_file(path);
        std::ostringstream out;
        write_serve_page(session, out);
        res.set_content(out.str(), "text/html; charset=utf-8");
    });

    server.Get("/api/session", [&path](httplib::Request const&, httplib::Response& res) {
        auto const session = load_file(path);
        res.set_content(session_to_json(session), "application/json");
    });

    std::cout << "chronicle-cli serve: " << path << " on http://127.0.0.1:" << port
              << " (Ctrl+C to stop)\n";
    if (!server.listen("127.0.0.1", port)) {
        std::cerr << "error: failed to bind to port " << port << "\n";
        return 1;
    }
    return 0;
}

} // namespace chronicle_cli
