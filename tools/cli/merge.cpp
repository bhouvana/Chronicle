#include "merge.hpp"

#include <chronicle/io/format.hpp>
#include <chronicle/io/wire.hpp>

using namespace chronicle::io;

namespace chronicle_cli {

// Writes the exact same on-disk shape session_writer.hpp's write<T>()
// overloads produce, just from already-loaded WireValue-level data instead
// of live tracked<T>/tracked_vector<T>/tracked_map<K,V> objects -- this
// tool never had (and structurally cannot reconstruct) the producer's
// original C++ types, only what loaded_session.hpp already parsed
// (docs/adr/0005-cli-requires-on-disk-format.md's process-separation rule
// applies here just as much as to every other chronicle-cli subcommand).
// Output is a normal, valid v4 session file: every existing chronicle-cli
// subcommand (list/history/diff/diff-runs/export) works on the merged
// result unmodified.
void write_merged_session(
    std::vector<std::pair<std::string, LoadedSession>> const& tagged_inputs, std::ostream& out) {
    write_header(out, CompressionKind::None);

    for (auto const& [tag, session] : tagged_inputs) {
        for (auto const& stream : session.streams) {
            write_string(out, tag + "." + stream.name);
            write_u8(out, static_cast<std::uint8_t>(stream.shape));
            write_u64(out, stream.events.size());
            for (auto const& event : stream.events) {
                // version/elapsed_ns/thread_hash are preserved verbatim
                // from the source process -- real data, not fabricated --
                // but they only ever compared meaningfully *within* the
                // stream they came from even before merging (ADR 0003),
                // and merging several processes' files together doesn't
                // change that: two events from different input files have
                // no established time relationship, merged or not. See
                // docs/adr/0028 for why this tool doesn't attempt to
                // synthesize one.
                write_u64(out, event.version);
                write_i64(out, event.elapsed_ns);
                write_u64(out, event.thread_hash);
                // Round-trip the call site through its own writer: this
                // tool has no std::source_location to reconstruct, only
                // the CallSiteInfo loaded_session.hpp already parsed.
                write_string(out, event.call_site.file);
                write_u64(out, event.call_site.line);
                write_u64(out, event.call_site.column);
                write_string(out, event.call_site.function);
                write_hlc(out, event.hlc);
                if (stream.shape == StreamShape::Scalar) {
                    write_wire_value(out, event.value);
                } else {
                    write_u8(out, static_cast<std::uint8_t>(event.op_kind));
                    write_wire_value(out, event.key_or_index);
                    write_wire_value(out, event.value);
                }
            }
        }
    }
}

} // namespace chronicle_cli
