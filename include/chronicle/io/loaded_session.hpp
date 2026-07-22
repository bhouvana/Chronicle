#pragma once

#include <cstdint>
#include <istream>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "chronicle/container_op.hpp" // ContainerOpKind only
#include "chronicle/io/format.hpp"
#include "chronicle/io/wire.hpp"

// The generic, type-erased view of a .chronicle session file -- what
// chronicle-cli actually operates on. chronicle-cli is a separate process
// from whatever produced the file and, unlike the writer side
// (session_writer.hpp), cannot know the producer's original C++ types
// (docs/adr/0005-cli-requires-on-disk-format.md is what put the CLI here in
// the first place). There is no T anywhere in this file: every value is a
// WireValue, which is exactly what makes this possible -- see wire.hpp's
// "closed vocabulary" rationale.
//
// Best-effort on truncated/corrupt input: this is a v0.2 tool reading files
// this project's own writer produced, not a hardened parser for untrusted
// input. A genuinely truncated file may produce a partial last stream
// rather than a clean error -- not a design goal to fix without a reason to.

namespace chronicle::io {

struct LoadedEvent {
    std::uint64_t version = 0;
    std::int64_t elapsed_ns = 0;
    std::uint64_t thread_hash = 0;
    ContainerOpKind op_kind = ContainerOpKind::Insert; // meaningful for IndexedOp/KeyedOp only
    WireValue key_or_index;                            // meaningful for IndexedOp/KeyedOp only
    WireValue value;
    CallSiteInfo call_site; // call_site.known() == false for plain `field = value` assignment
};

struct LoadedStream {
    std::string name;
    StreamShape shape = StreamShape::Scalar;
    std::vector<LoadedEvent> events;
};

struct LoadedSession {
    std::vector<LoadedStream> streams;

    [[nodiscard]] LoadedStream const* find(std::string const& name) const {
        for (auto const& stream : streams) {
            if (stream.name == name) {
                return &stream;
            }
        }
        return nullptr;
    }
};

namespace detail {

[[nodiscard]] inline LoadedSession parse_session_body(std::istream& is) {
    LoadedSession session;

    while (true) {
        // read_u64 at true EOF sets eofbit without extracting anything --
        // that's the intended, only block-boundary terminator (no leading
        // stream count in this format, see session_writer.hpp).
        auto const name_len = read_u64(is);
        if (is.eof()) {
            break;
        }

        LoadedStream stream;
        stream.name.resize(name_len);
        is.read(stream.name.data(), static_cast<std::streamsize>(name_len));
        stream.shape = static_cast<StreamShape>(read_u8(is));

        auto const event_count = read_u64(is);
        stream.events.reserve(event_count);
        for (std::uint64_t i = 0; i < event_count; ++i) {
            LoadedEvent event;
            event.version = read_u64(is);
            event.elapsed_ns = read_i64(is);
            event.thread_hash = read_u64(is);
            event.call_site = read_call_site(is);
            if (stream.shape == StreamShape::Scalar) {
                event.value = read_wire_value(is);
            } else {
                event.op_kind = static_cast<ContainerOpKind>(read_u8(is));
                event.key_or_index = read_wire_value(is);
                event.value = read_wire_value(is);
            }
            stream.events.push_back(std::move(event));
        }
        session.streams.push_back(std::move(stream));

        if (!is.good()) {
            break;
        }
    }
    return session;
}

} // namespace detail

// `codecs`: compression implementations this caller supports -- pass
// `{chronicle::io::zstd_codec()}` (chronicle/io/zstd_codec.hpp) to
// transparently read Zstd-compressed files; leave empty (the default) to
// only support uncompressed (CompressionKind::None) files, with a clear
// error rather than a misparse if the file turns out to be compressed.
// This function itself stays codec-agnostic either way -- see
// docs/adr/0014-storage-engine-compression.md.
[[nodiscard]] inline LoadedSession load_session(std::istream& is,
                                                  std::span<CompressionCodec const> codecs = {}) {
    CompressionKind const compression = read_header(is);
    if (compression == CompressionKind::None) {
        return detail::parse_session_body(is);
    }

    for (auto const& codec : codecs) {
        if (codec.kind != compression) {
            continue;
        }
        auto const compressed_size = read_u64(is);
        std::string compressed(compressed_size, '\0');
        is.read(compressed.data(), static_cast<std::streamsize>(compressed_size));
        std::string const decompressed = codec.decompress(compressed);
        std::istringstream body(decompressed);
        return detail::parse_session_body(body);
    }
    throw std::runtime_error(
        "session file uses an unsupported compression codec (kind=" +
        std::to_string(static_cast<int>(compression)) +
        ") -- pass a matching CompressionCodec to load_session(), e.g. chronicle::io::zstd_codec()");
}

} // namespace chronicle::io
