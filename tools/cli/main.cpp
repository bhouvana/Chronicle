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
#include "doctor.hpp"
#include "html_export.hpp"
#include "merge.hpp"
#include "narrate.hpp"
#include "object_graph.hpp"
#include "perfetto_export.hpp"
#include "query.hpp"
#include "replay.hpp"
#include "serve.hpp"
#include "session_loader.hpp"

#include <chronicle/io/loaded_session.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
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

// docs/12-future-research-topics.md topic 6: combines N already-captured
// per-process session files into one, namespaced by tag. `args` are
// "<tag>:<path>" pairs -- ':' rather than '=' so a Windows drive-letter
// path ("C:\...") only ever supplies one extra ':' to split on, handled by
// splitting at the *first* ':' only.
int cmd_merge(std::string const& output_path, std::vector<std::string> const& tagged_args) {
    std::vector<std::pair<std::string, chronicle::io::LoadedSession>> inputs;
    for (auto const& arg : tagged_args) {
        auto const colon = arg.find(':');
        if (colon == std::string::npos) {
            std::cerr << "error: expected <tag>:<path>, got: " << arg << "\n";
            return 1;
        }
        std::string const tag = arg.substr(0, colon);
        std::string const path = arg.substr(colon + 1);
        inputs.emplace_back(tag, load_file(path));
    }
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open output file: " + output_path);
    }
    chronicle_cli::write_merged_session(inputs, out);
    std::size_t total_streams = 0;
    for (auto const& [tag, session] : inputs) {
        total_streams += session.streams.size();
    }
    std::cout << "wrote " << output_path << " (" << inputs.size() << " process(es), " << total_streams
              << " stream(s) total)\n";
    return 0;
}

// docs/adr/0031-object-graph.md ("Layer 2"): a CLI-visible version of what
// ADR 0016's browser viewer already shows in its Objects panel, grouped by
// chronicle::object_name_of() (tools/cli/object_graph.cpp).
int cmd_objects(std::string const& path) {
    auto const session = load_file(path);
    auto const groups = chronicle_cli::group_by_object(session);
    for (auto const& group : groups) {
        std::size_t total_events = 0;
        for (auto const* field : group.fields) {
            total_events += field->events.size();
        }
        std::cout << group.name << "  (" << group.fields.size() << " field(s), " << total_events
                   << " event(s) total)\n";
        for (auto const* field : group.fields) {
            std::cout << "    " << field->name << "  [" << shape_name(field->shape) << "]  "
                       << field->events.size() << " event(s)\n";
        }
    }
    return 0;
}

// Merges every field under `object_name` into one chronologically-ordered
// log -- a text-mode "scrub through this whole object's history" (the
// user's Layer 5 ask), not just one field at a time. Best-effort ordering
// across independent streams: prefers the HLC (ADR 0019's one real
// cross-stream-comparable ordinal) only when EVERY merged event has one;
// otherwise falls back to elapsed_ns. Never claims stronger cross-stream
// ordering than ADR 0003/0019 already established.
int cmd_object_history(std::string const& path, std::string const& object_name) {
    auto const session = load_file(path);
    auto const groups = chronicle_cli::group_by_object(session);
    auto const it = std::find_if(groups.begin(), groups.end(),
                                  [&](auto const& g) { return g.name == object_name; });
    if (it == groups.end() || it->fields.empty()) {
        std::cerr << "no such object (or it has no recorded fields): " << object_name << "\n";
        return 1;
    }

    auto const merged = chronicle_cli::merge_object_history(*it);
    std::cout << "object-history for " << object_name << " ("
               << (merged.ordered_by_hlc ? "ordered by HLC" : "ordered by elapsed_ns, best-effort") << "):\n";
    for (auto const& entry : merged.entries) {
        std::cout << entry.field_name << "  ";
        print_event(*entry.event, entry.shape);
    }
    return 0;
}

// docs/adr/0034-object-snapshot.md ("Layer 5" -- "one slider, entire
// object"): reconstructs every field's value under `object_name` as of
// `position` (an index into the object's own merged, chronologically-
// ordered event log -- the same log `object-history` prints in full).
// Position, not a raw version or timestamp: cross-stream version numbers
// have no shared meaning (ADR 0003), and this stays honest about that by
// making "the slider" an ordinal into the one merge this project can
// actually justify (chronicle_cli::merge_object_history), not a
// fabricated absolute clock position.
// Shared by cmd_object_snapshot and cmd_program_snapshot (docs/adr/0036,
// "Layer 6, scoped"): folds a merged event log up to `cutoff` and prints
// every field's reconstructed value -- the only difference between "one
// object's snapshot" and "the whole program's snapshot" is which fields
// are in `fields` and which merge produced `merged`, not the folding
// logic itself.
void print_fields_snapshot(std::vector<LoadedStream const*> const& fields,
                            chronicle_cli::MergedObjectHistory const& merged, std::size_t cutoff) {
    for (auto const& snap : chronicle_cli::snapshot_fields(fields, merged, cutoff)) {
        std::cout << "  " << snap.field_name << ": " << (snap.recorded ? snap.rendered : "(not yet recorded)")
                   << "\n";
    }
}

int cmd_object_snapshot(std::string const& path, std::string const& object_name, std::size_t position) {
    auto const session = load_file(path);
    auto const groups = chronicle_cli::group_by_object(session);
    auto const it = std::find_if(groups.begin(), groups.end(),
                                  [&](auto const& g) { return g.name == object_name; });
    if (it == groups.end() || it->fields.empty()) {
        std::cerr << "no such object (or it has no recorded fields): " << object_name << "\n";
        return 1;
    }

    auto const merged = chronicle_cli::merge_object_history(*it);
    if (merged.entries.empty()) {
        std::cerr << "object has no recorded events: " << object_name << "\n";
        return 1;
    }
    std::size_t const cutoff = std::min(position, merged.entries.size() - 1);

    std::cout << "object-snapshot for " << object_name << " at position " << cutoff << " of "
               << (merged.entries.size() - 1) << " ("
               << (merged.ordered_by_hlc ? "ordered by HLC" : "ordered by elapsed_ns, best-effort") << "):\n";
    print_fields_snapshot(it->fields, merged, cutoff);
    return 0;
}

// docs/adr/0036-whole-program-rewind.md ("Layer 6, scoped"): the same
// merge/fold as object-history/object-snapshot, over every stream in the
// session instead of one object's fields. Deliberately NOT a claim of
// memory-level determinism -- "rewind everything Chronicle actually
// instruments," best-effort across threads, exactly like every other
// cross-stream merge this project ships (ADR 0003/0019's standing
// caveat applies here at full-program scope, not a stronger one).
int cmd_program_history(std::string const& path) {
    auto const session = load_file(path);
    auto const merged = chronicle_cli::merge_entire_session(session);
    std::cout << "program-history (" << merged.entries.size() << " event(s) across " << session.streams.size()
               << " stream(s), "
               << (merged.ordered_by_hlc ? "ordered by HLC" : "ordered by elapsed_ns, best-effort") << "):\n";
    for (auto const& entry : merged.entries) {
        std::cout << entry.field_name << "  ";
        print_event(*entry.event, entry.shape);
    }
    return 0;
}

int cmd_program_snapshot(std::string const& path, std::size_t position) {
    auto const session = load_file(path);
    auto const merged = chronicle_cli::merge_entire_session(session);
    if (merged.entries.empty()) {
        std::cerr << "session has no recorded events\n";
        return 1;
    }
    std::size_t const cutoff = std::min(position, merged.entries.size() - 1);

    std::vector<LoadedStream const*> all_fields;
    all_fields.reserve(session.streams.size());
    for (auto const& stream : session.streams) {
        all_fields.push_back(&stream);
    }

    std::cout << "program-snapshot at position " << cutoff << " of " << (merged.entries.size() - 1) << " ("
               << (merged.ordered_by_hlc ? "ordered by HLC" : "ordered by elapsed_ns, best-effort") << "):\n";
    print_fields_snapshot(all_fields, merged, cutoff);
    return 0;
}

// docs/adr/0041-doctor-and-rules.md: "people love tools that tell them
// what's wrong before they know what to ask" -- pure composition of
// existing, already-verified primitives (group_by_object, most_changed_streams,
// thread_index, merge_entire_session) plus two new whole-file detection
// passes (tools/cli/doctor.cpp). Exit code reflects health, so this is
// usable as a CI gate, not just an interactive report.
int cmd_doctor(std::string const& path) {
    auto const session = load_file(path);
    bool const issues_found = chronicle_cli::write_doctor_report(session, std::cout);
    return issues_found ? 1 : 0;
}

// docs/adr/0038-narrative-composer.md ("Layer 10"): composes existing,
// already-persisted primitives (object-snapshot values, call sites, a
// possible_race()-mirroring pass, a container-growth pass) into one
// human-readable report -- an emergent capability, not a new recording
// mechanism.
int cmd_narrate(std::string const& path, std::string const& object_name, std::size_t position) {
    auto const session = load_file(path);
    chronicle_cli::write_narration(session, object_name, position, std::cout);
    return 0;
}

// docs/adr/0035-live-queries.md ("Layer 7"): a small set of real,
// concrete aggregate queries over an already-loaded session -- not a
// general query language, three specific answers this project can
// actually justify from the data it has.
int cmd_query_most_changed(std::string const& path) {
    auto const session = load_file(path);
    auto const activity = chronicle_cli::most_changed_streams(session);
    for (auto const& entry : activity) {
        std::cout << entry.name << "  " << entry.event_count << " event(s)\n";
    }
    return 0;
}

int cmd_query_threads(std::string const& path) {
    auto const session = load_file(path);
    auto const threads = chronicle_cli::thread_index(session);
    for (std::size_t i = 0; i < threads.size(); ++i) {
        std::size_t event_count = 0;
        for (auto const& stream : session.streams) {
            for (auto const& event : stream.events) {
                if (event.thread_hash == threads[i]) {
                    ++event_count;
                }
            }
        }
        std::cout << "thread " << i << "  (raw hash " << threads[i] << ")  " << event_count << " event(s)\n";
    }
    return 0;
}

int cmd_query_thread(std::string const& path, std::size_t thread_index_arg) {
    auto const session = load_file(path);
    auto const threads = chronicle_cli::thread_index(session);
    if (thread_index_arg >= threads.size()) {
        std::cerr << "no such thread index: " << thread_index_arg << " (session has " << threads.size()
                   << " distinct thread(s), indices 0.." << (threads.empty() ? 0 : threads.size() - 1) << ")\n";
        return 1;
    }
    auto const merged = chronicle_cli::events_from_thread(session, threads[thread_index_arg]);
    std::cout << "writes from thread " << thread_index_arg << " ("
               << (merged.ordered_by_hlc ? "ordered by HLC" : "ordered by elapsed_ns, best-effort") << "):\n";
    for (auto const& entry : merged.entries) {
        std::cout << entry.field_name << "  ";
        print_event(*entry.event, entry.shape);
    }
    return 0;
}

void print_usage() {
    std::cout << "usage:\n"
                 "  chronicle-cli list <file>\n"
                 "  chronicle-cli history <file> <stream-name>\n"
                 "  chronicle-cli diff <file> <stream-name> <version-a> <version-b>\n"
                 "  chronicle-cli diff-runs <file-a> <file-b>\n"
                 "  chronicle-cli merge <output.chronicle> <tag1>:<file1> [<tag2>:<file2> ...]\n"
                 "  chronicle-cli objects <file>\n"
                 "  chronicle-cli object-history <file> <object-name>\n"
                 "  chronicle-cli object-snapshot <file> <object-name> <position>\n"
                 "  chronicle-cli query most-changed <file>\n"
                 "  chronicle-cli query threads <file>\n"
                 "  chronicle-cli query thread <file> <thread-index>\n"
                 "  chronicle-cli program-history <file>\n"
                 "  chronicle-cli program-snapshot <file> <position>\n"
                 "  chronicle-cli narrate <file> <object-name> <position>\n"
                 "  chronicle-cli doctor <file>\n"
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
        if (args[0] == "merge" && args.size() >= 3) {
            return cmd_merge(args[1], std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args[0] == "objects" && args.size() == 2) {
            return cmd_objects(args[1]);
        }
        if (args[0] == "object-history" && args.size() == 3) {
            return cmd_object_history(args[1], args[2]);
        }
        if (args[0] == "object-snapshot" && args.size() == 4) {
            return cmd_object_snapshot(args[1], args[2], static_cast<std::size_t>(std::stoull(args[3])));
        }
        if (args[0] == "query" && args.size() == 3 && args[1] == "most-changed") {
            return cmd_query_most_changed(args[2]);
        }
        if (args[0] == "query" && args.size() == 3 && args[1] == "threads") {
            return cmd_query_threads(args[2]);
        }
        if (args[0] == "query" && args.size() == 4 && args[1] == "thread") {
            return cmd_query_thread(args[2], static_cast<std::size_t>(std::stoull(args[3])));
        }
        if (args[0] == "program-history" && args.size() == 2) {
            return cmd_program_history(args[1]);
        }
        if (args[0] == "program-snapshot" && args.size() == 3) {
            return cmd_program_snapshot(args[1], static_cast<std::size_t>(std::stoull(args[2])));
        }
        if (args[0] == "narrate" && args.size() == 4) {
            return cmd_narrate(args[1], args[2], static_cast<std::size_t>(std::stoull(args[3])));
        }
        if (args[0] == "doctor" && args.size() == 2) {
            return cmd_doctor(args[1]);
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
