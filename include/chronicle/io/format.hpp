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

#include "chronicle/io/wire.hpp"

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
// v3 (docs/adr/0014-storage-engine-compression.md): the header now carries a
// one-byte CompressionKind tag (see below), always written/read in the
// clear so any reader can decide how to interpret the rest of the file
// before touching a single compressed byte. A breaking, non-backward-
// compatible bump from v2 -- v1/v2 files can no longer be read. No
// compatibility requirement exists yet for this format (docs/11's CI/
// versioning story hasn't been reached), so this is a clean break, not a
// migration, same as the v1->v2 bump before it.
inline constexpr std::uint32_t kFormatVersion = 3;

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

} // namespace chronicle::io
