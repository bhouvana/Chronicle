// chronicle-cli: reads a .chronicle session file and answers "what
// happened" from the command line -- the tool docs/08-visualization.md
// calls the always-available, dependency-free tier, and the one
// docs/adr/0005-cli-requires-on-disk-format.md moved to v0.2 once it had a
// file format to actually read. Runs as a fully separate process from
// whatever produced the file (docs/05-architecture.md's process-separation
// rule) and links only against chronicle-core's io headers -- no knowledge
// of the producer's original C++ types, only WireValues (see
// chronicle/io/loaded_session.hpp).

#include "diff_runs.hpp"
#include "html_export.hpp"
#include "perfetto_export.hpp"
#include "replay.hpp"
#include "serve.hpp"
#include "session_loader.hpp"

#include <chronicle/io/loaded_session.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using chronicle::ContainerOpKind;
using namespace chronicle::io;
using chronicle_cli::load_file;
#ifdef CHRONICLE_CLI_HAVE_HTTPLIB
using chronicle_cli::cmd_serve;
#endif

namespace {

std::string shape_name(StreamShape s) {
    switch (s) {
        case StreamShape::Scalar: return "scalar";
        case StreamShape::IndexedOp: return "indexed (vector)";
        case StreamShape::KeyedOp: return "keyed (map)";
    }
    return "?";
}

std::string op_kind_name(ContainerOpKind k) {
    switch (k) {
        case ContainerOpKind::Insert: return "insert";
        case ContainerOpKind::Erase: return "erase";
        case ContainerOpKind::Update: return "update";
        case ContainerOpKind::Clear: return "clear";
    }
    return "?";
}

int cmd_list(std::string const& path) {
    auto const session = load_file(path);
    for (auto const& stream : session.streams) {
        std::cout << stream.name << "  [" << shape_name(stream.shape) << "]  "
                  << stream.events.size() << " event(s)\n";
    }
    return 0;
}

// docs/10-roadmap.md's v0.5 causal-chain-queries item, surfaced in the CLI:
// shows just the filename, not the full path, since the full path is where
// the *producing machine's* build tree happened to be, rarely useful to a
// reader on a different machine. call_site.known() == false for events
// recorded via plain `field = value` (docs/adr/0010) -- shown as nothing
// rather than a fabricated "unknown:0" that could be mistaken for real data.
std::string call_site_suffix(CallSiteInfo const& call_site) {
    if (!call_site.known()) {
        return "";
    }
    auto const slash = call_site.file.find_last_of("/\\");
    std::string const filename =
        slash == std::string::npos ? call_site.file : call_site.file.substr(slash + 1);
    return "  [" + filename + ":" + std::to_string(call_site.line) + "]";
}

void print_event(LoadedEvent const& event, StreamShape shape) {
    std::cout << "  v" << event.version << " (+" << event.elapsed_ns << "ns): ";
    if (shape == StreamShape::Scalar) {
        std::cout << to_display_string(event.value);
    } else {
        std::cout << op_kind_name(event.op_kind) << "[" << to_display_string(event.key_or_index) << "]";
        if (event.op_kind == ContainerOpKind::Insert || event.op_kind == ContainerOpKind::Update) {
            std::cout << " = " << to_display_string(event.value);
        }
    }
    std::cout << call_site_suffix(event.call_site) << "\n";
}

int cmd_history(std::string const& path, std::string const& stream_name) {
    auto const session = load_file(path);
    auto const* stream = session.find(stream_name);
    if (stream == nullptr) {
        std::cerr << "no such stream: " << stream_name << "\n";
        return 1;
    }
    for (auto const& event : stream->events) {
        print_event(event, stream->shape);
    }
    return 0;
}

using chronicle_cli::replay_indexed;
using chronicle_cli::replay_keyed;

int cmd_diff(std::string const& path, std::string const& stream_name, std::uint64_t v0, std::uint64_t v1) {
    auto const session = load_file(path);
    auto const* stream = session.find(stream_name);
    if (stream == nullptr) {
        std::cerr << "no such stream: " << stream_name << "\n";
        return 1;
    }

    if (stream->shape == StreamShape::Scalar) {
        std::optional<WireValue> before, after;
        for (auto const& event : stream->events) {
            if (event.version <= v0) {
                before = event.value;
            }
            if (event.version <= v1) {
                after = event.value;
            }
        }
        std::cout << "v" << v0 << ": " << (before ? to_display_string(*before) : "<none>") << "\n";
        std::cout << "v" << v1 << ": " << (after ? to_display_string(*after) : "<none>") << "\n";
        return 0;
    }

    if (stream->shape == StreamShape::IndexedOp) {
        auto const a = replay_indexed(*stream, v0);
        auto const b = replay_indexed(*stream, v1);
        auto const common = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < common; ++i) {
            if (a[i] != b[i]) {
                std::cout << "  [" << i << "]: " << to_display_string(a[i]) << " -> "
                          << to_display_string(b[i]) << "\n";
            }
        }
        for (std::size_t i = common; i < b.size(); ++i) {
            std::cout << "  [" << i << "]: (new) -> " << to_display_string(b[i]) << "\n";
        }
        for (std::size_t i = common; i < a.size(); ++i) {
            std::cout << "  [" << i << "]: " << to_display_string(a[i]) << " -> (removed)\n";
        }
        return 0;
    }

    // KeyedOp
    auto const a = replay_keyed(*stream, v0);
    auto const b = replay_keyed(*stream, v1);
    for (auto const& [key, value] : a) {
        auto it = std::find_if(b.begin(), b.end(), [&](auto const& kv) { return kv.first == key; });
        if (it == b.end()) {
            std::cout << "  " << to_display_string(key) << ": " << to_display_string(value)
                      << " -> (removed)\n";
        } else if (it->second != value) {
            std::cout << "  " << to_display_string(key) << ": " << to_display_string(value) << " -> "
                      << to_display_string(it->second) << "\n";
        }
    }
    for (auto const& [key, value] : b) {
        auto it = std::find_if(a.begin(), a.end(), [&](auto const& kv) { return kv.first == key; });
        if (it == a.end()) {
            std::cout << "  " << to_display_string(key) << ": (new) -> " << to_display_string(value)
                      << "\n";
        }
    }
    return 0;
}

// docs/08-visualization.md tier 2: static, self-contained HTML timeline
// viewer -- no server, shareable as a build/CI artifact. See html_export.cpp
// for the page itself; this just wires the CLI to it.
int cmd_export_html(std::string const& path, std::string const& output_path) {
    auto const session = load_file(path);
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open output file: " + output_path);
    }
    chronicle_cli::write_html_export(session, out);
    std::cout << "wrote " << output_path << " (" << session.streams.size() << " stream(s))\n";
    return 0;
}

// docs/adr/0020-perfetto-export-bridge.md: reuses Perfetto's timeline UI
// (Chrome JSON trace format) instead of Chronicle building a second one.
int cmd_export_perfetto(std::string const& path, std::string const& output_path) {
    auto const session = load_file(path);
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open output file: " + output_path);
    }
    chronicle_cli::write_perfetto_export(session, out);
    std::cout << "wrote " << output_path << " (" << session.streams.size()
              << " stream(s)) -- open at https://ui.perfetto.dev\n";
    return 0;
}

// docs/12-future-research-topics.md topic 5: two independently-produced
// session files, aligned by stream name (see diff_runs.cpp for why name,
// not object identity/version). "Same scenario" is the caller's claim, not
// something this tool verifies -- it diffs whatever two files it's given.
int cmd_diff_runs(std::string const& path_a, std::string const& path_b) {
    auto const run_a = load_file(path_a);
    auto const run_b = load_file(path_b);
    bool const differs = chronicle_cli::write_run_diff(run_a, run_b, std::cout);
    return differs ? 1 : 0;
}

void print_usage() {
    std::cout << "usage:\n"
                 "  chronicle-cli list <file>\n"
                 "  chronicle-cli history <file> <stream-name>\n"
                 "  chronicle-cli diff <file> <stream-name> <version-a> <version-b>\n"
                 "  chronicle-cli diff-runs <file-a> <file-b>\n"
                 "  chronicle-cli export --html <file> <output.html>\n"
                 "  chronicle-cli export --perfetto <file> <output.json>\n"
#ifdef CHRONICLE_CLI_HAVE_HTTPLIB
                 "  chronicle-cli serve <file> [--port N]\n"
#endif
        ;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> const args(argv + 1, argv + argc);
    try {
        if (args.empty()) {
            print_usage();
            return 1;
        }
        if (args[0] == "list" && args.size() == 2) {
            return cmd_list(args[1]);
        }
        if (args[0] == "history" && args.size() == 3) {
            return cmd_history(args[1], args[2]);
        }
        if (args[0] == "diff" && args.size() == 5) {
            return cmd_diff(args[1], args[2], std::stoull(args[3]), std::stoull(args[4]));
        }
        if (args[0] == "diff-runs" && args.size() == 3) {
            return cmd_diff_runs(args[1], args[2]);
        }
        if (args[0] == "export" && args.size() == 4 && args[1] == "--html") {
            return cmd_export_html(args[2], args[3]);
        }
        if (args[0] == "export" && args.size() == 4 && args[1] == "--perfetto") {
            return cmd_export_perfetto(args[2], args[3]);
        }
#ifdef CHRONICLE_CLI_HAVE_HTTPLIB
        if (args[0] == "serve" && (args.size() == 2 || args.size() == 4)) {
            int port = 8080;
            if (args.size() == 4) {
                if (args[2] != "--port") {
                    print_usage();
                    return 1;
                }
                port = std::stoi(args[3]);
            }
            return cmd_serve(args[1], port);
        }
#endif
        print_usage();
        return 1;
    } catch (std::exception const& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
