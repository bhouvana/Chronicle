#pragma once

#include <chrono>
#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "chronicle/derived.hpp"
#include "chronicle/hlc.hpp"
#include "chronicle/io/wire.hpp"
#include "chronicle/provenance.hpp"

// Shared file-format constants and helpers, used by both the write side
// (session_writer.hpp, knows T) and the read side (loaded_session.hpp,
// deliberately does not -- see docs/adr/0008-cli-avoids-streambase-virtual-dispatch.md).

namespace chronicle::io {

// How to interpret an event's payload bytes without knowing the producer's
// original T -- see wire.hpp's "closed vocabulary" rationale.
enum class StreamShape : std::uint8_t {
    Scalar = 0,    // one WireValue per event
    IndexedOp = 1, // op_kind(u8) + index(WireValue) + value(WireValue) per event
    KeyedOp = 2,   // op_kind(u8) + key(WireValue) + value(WireValue) per event
};

inline constexpr char kMagic[4] = {'C', 'H', 'R', 'N'};
// v4 (docs/adr/0019-hybrid-logical-clock.md): every event header now also
// carries an HlcTimestamp (physical_us + logical), right after call_site --
// docs/06-recording-model.md and docs/adr/0003-causal-not-global-ordering.md
// both describe this as filling an already-"reserved" field; verified
// directly that no such field existed anywhere in the prior format, so this
// is a genuinely new field, not a reserved slot being filled in. A
// breaking, non-backward-compatible bump from v3 -- v1/v2/v3 files can no
// longer be read. No compatibility requirement exists yet for this format
// (docs/11's CI/versioning story hasn't been reached), so this is a clean
// break, not a migration, same as every prior bump.
//
// v5 (docs/adr/0039-persist-provenance-and-derivation.md): two new,
// optional extended sections follow the last stream block -- a
// provenance table (chronicle::provenance::StackFrame chains from
// ADR 0032's set_with_stacktrace(), keyed by stream name + version) and a
// derivation table (chronicle::derived::DependencyChange lists from
// ADR 0033's derive()/explain(), same keying). Both registries this data
// comes from are in-process-only singletons keyed by a stream's
// process-local id() (ADR 0032); persisting means translating that key to
// the one identifier that survives a save/load round trip -- the stream's
// on-disk name. Another clean, breaking bump, same convention as v1-v4.
inline constexpr std::uint32_t kFormatVersion = 5;

// A stream name-length value no real stream could ever have (an
// exabyte-scale string) -- the explicit terminator for the stream-block
// loop in v5+ files. Needed because the format has no leading stream
// count (session_writer.hpp's own "single-pass, self-terminating"
// design) and previously relied on genuine end-of-file to know when
// streams were done; v5 adds real data *after* the streams, so "real
// EOF" can no longer be the stream loop's stop condition. Same "an
// otherwise-impossible value is a real, unambiguous sentinel" convention
// this format already uses for HlcTimestamp{0,0} and call_site.line==0.
inline constexpr std::uint64_t kExtendedSectionsMarker = 0xFFFFFFFFFFFFFFFFull;

// Everything after this tag in the file is either the plain payload
// (None) or exactly one compressed blob covering the *entire* rest of the
// file, `[compressed_size: u64][compressed_bytes]` (any kind other than
// None) -- see docs/adr/0014-storage-engine-compression.md for why the
// whole post-header payload is one blob rather than per-stream or
// per-event chunks. New kinds are additive: an old reader that doesn't
// recognize a kind byte fails with a clear "unsupported codec" error
// (see read_header below / load_session), not a silent misparse of
// compressed bytes as if they were the plain format.
enum class CompressionKind : std::uint8_t {
    None = 0,
    Zstd = 1,
};

// Deliberately zero-dependency: format.hpp (and everything that includes
// it -- session_writer.hpp, loaded_session.hpp) never links a compression
// library itself. A codec is a plain pair of function pointers the caller
// supplies -- from chronicle/io/zstd_codec.hpp for Zstd, or any other
// implementation for a future kind -- the same "runtime extension point,
// not a forced dependency" shape as Stream<T>::RecordHook
// (docs/adr/0013-tracy-bridge.md). Consumers who never touch compression
// (most of chronicle-core, and any tool that only reads uncompressed
// files) pay nothing and link nothing extra.
struct CompressionCodec {
    CompressionKind kind;
    std::string (*compress)(std::string_view);
    std::string (*decompress)(std::string_view);
};

inline void write_header(std::ostream& os, CompressionKind compression = CompressionKind::None) {
    os.write(kMagic, sizeof(kMagic));
    write_u64(os, kFormatVersion);
    write_u8(os, static_cast<std::uint8_t>(compression));
}

// Cold-path I/O boundary -- an exception is the right tool here, unlike on
// the hot recording path, which never throws (docs/07's API design rule).
// Returns the compression tag so the caller (load_session) can decide
// whether/how to decompress everything that follows; this function itself
// never does, keeping it (and format.hpp as a whole) codec-agnostic.
[[nodiscard]] inline CompressionKind read_header(std::istream& is) {
    char magic[4] = {};
    is.read(magic, sizeof(magic));
    if (is.gcount() != 4 || std::memcmp(magic, kMagic, 4) != 0) {
        throw std::runtime_error("not a chronicle session file (bad magic)");
    }
    auto const version = read_u64(is);
    if (version != kFormatVersion) {
        throw std::runtime_error("unsupported chronicle session format version");
    }
    return static_cast<CompressionKind>(read_u8(is));
}

inline std::int64_t elapsed_ns(std::chrono::steady_clock::time_point t,
                                std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t - start).count();
}

inline std::uint64_t thread_hash(std::thread::id id) { return std::hash<std::thread::id>{}(id); }

inline void write_call_site(std::ostream& os, std::source_location const& loc) {
    write_string(os, loc.file_name());
    write_u64(os, loc.line());
    write_u64(os, loc.column());
    write_string(os, loc.function_name());
}

// The generic, type-erased view of a call site -- what loaded_session.hpp
// works with, since (like the rest of that file) it has no C++ source_location
// object to reconstruct, only the raw bytes a writer produced (see
// chronicle/io/session_writer.hpp). line() == 0 means "not captured for this
// event" (docs/adr/0010): tracked<T>::operator= records an empty
// std::source_location{} for exactly this reason, and write_call_site above
// faithfully serializes that as line 0 rather than inventing a fallback.
struct CallSiteInfo {
    std::string file;
    std::uint64_t line = 0;
    std::uint64_t column = 0;
    std::string function;

    [[nodiscard]] bool known() const noexcept { return line != 0; }
};

inline CallSiteInfo read_call_site(std::istream& is) {
    CallSiteInfo info;
    info.file = read_string(is);
    info.line = read_u64(is);
    info.column = read_u64(is);
    info.function = read_string(is);
    return info;
}

// docs/adr/0019-hybrid-logical-clock.md: HlcTimestamp{} (physical_us == 0
// && logical == 0) means "not computed for this event" (the producing
// Session's Config::causal_clock was false), same sentinel convention as
// call_site's line == 0 -- faithfully serialized as zero rather than
// inventing a fallback, exactly like write_call_site above.
inline void write_hlc(std::ostream& os, HlcTimestamp const& hlc) {
    write_u64(os, hlc.physical_us);
    write_u64(os, hlc.logical); // widened to u64 on the wire; logical fits in 20 bits in memory
}

inline HlcTimestamp read_hlc(std::istream& is) {
    HlcTimestamp hlc;
    hlc.physical_us = read_u64(is);
    hlc.logical = static_cast<std::uint32_t>(read_u64(is));
    return hlc;
}

// docs/adr/0039-persist-provenance-and-derivation.md: shared between the
// write side (session_writer.hpp) and read side (loaded_session.hpp),
// same as everything else in this file -- keyed by stream **name**, not
// the in-process-only stream_id() the live registries
// (chronicle::provenance::Registry/chronicle::derived::Registry) actually
// use, since name is the one identifier that survives a save/load round
// trip.

inline void write_provenance_frame(std::ostream& os, provenance::StackFrame const& frame) {
    write_string(os, frame.description);
    write_string(os, frame.source_file);
    write_u64(os, frame.source_line);
}

inline provenance::StackFrame read_provenance_frame(std::istream& is) {
    provenance::StackFrame frame;
    frame.description = read_string(is);
    frame.source_file = read_string(is);
    frame.source_line = static_cast<std::uint32_t>(read_u64(is));
    return frame;
}

struct ProvenanceEntry {
    std::string stream_name;
    std::uint64_t version = 0;
    std::vector<provenance::StackFrame> frames;
};

inline void write_provenance_entry(std::ostream& os, ProvenanceEntry const& entry) {
    write_string(os, entry.stream_name);
    write_u64(os, entry.version);
    write_u64(os, entry.frames.size());
    for (auto const& frame : entry.frames) {
        write_provenance_frame(os, frame);
    }
}

inline ProvenanceEntry read_provenance_entry(std::istream& is) {
    ProvenanceEntry entry;
    entry.stream_name = read_string(is);
    entry.version = read_u64(is);
    auto const frame_count = read_u64(is);
    entry.frames.reserve(frame_count);
    for (std::uint64_t i = 0; i < frame_count; ++i) {
        entry.frames.push_back(read_provenance_frame(is));
    }
    return entry;
}

inline void write_dependency_change(std::ostream& os, derived::DependencyChange const& change) {
    write_string(os, change.name);
    write_string(os, change.old_value);
    write_string(os, change.new_value);
    write_u8(os, change.changed ? 1 : 0);
}

inline derived::DependencyChange read_dependency_change(std::istream& is) {
    derived::DependencyChange change;
    change.name = read_string(is);
    change.old_value = read_string(is);
    change.new_value = read_string(is);
    change.changed = read_u8(is) != 0;
    return change;
}

struct DerivationEntry {
    std::string stream_name;
    std::uint64_t version = 0;
    std::vector<derived::DependencyChange> changes;
};

inline void write_derivation_entry(std::ostream& os, DerivationEntry const& entry) {
    write_string(os, entry.stream_name);
    write_u64(os, entry.version);
    write_u64(os, entry.changes.size());
    for (auto const& change : entry.changes) {
        write_dependency_change(os, change);
    }
}

inline DerivationEntry read_derivation_entry(std::istream& is) {
    DerivationEntry entry;
    entry.stream_name = read_string(is);
    entry.version = read_u64(is);
    auto const change_count = read_u64(is);
    entry.changes.reserve(change_count);
    for (std::uint64_t i = 0; i < change_count; ++i) {
        entry.changes.push_back(read_dependency_change(is));
    }
    return entry;
}

} // namespace chronicle::io
