#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "chronicle/io/format.hpp"

#include <zstd.h>

// docs/adr/0014-storage-engine-compression.md: the at-rest half of
// docs/06-recording-model.md's "Compression (LZ4 for live/low-latency
// streams, Zstd for at-rest session files)" line. Genuinely separate and
// optional, like tools/codegen and tracy_bridge.hpp before it -- nothing in
// chronicle-core, session_writer.hpp, or loaded_session.hpp includes this
// file or links libzstd unless a consumer explicitly opts in by including
// it and passing chronicle::io::zstd_codec() to SessionWriter/load_session.

namespace chronicle::io {

[[nodiscard]] inline std::string zstd_compress(std::string_view data) {
    std::string out(ZSTD_compressBound(data.size()), '\0');
    std::size_t const written =
        ZSTD_compress(out.data(), out.size(), data.data(), data.size(), ZSTD_CLEVEL_DEFAULT);
    if (ZSTD_isError(written)) {
        throw std::runtime_error(std::string("zstd compression failed: ") + ZSTD_getErrorName(written));
    }
    out.resize(written);
    return out;
}

[[nodiscard]] inline std::string zstd_decompress(std::string_view compressed) {
    unsigned long long const content_size =
        ZSTD_getFrameContentSize(compressed.data(), compressed.size());
    if (content_size == ZSTD_CONTENTSIZE_ERROR || content_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        throw std::runtime_error("zstd decompression failed: not a valid zstd frame with a known size");
    }
    std::string out(static_cast<std::size_t>(content_size), '\0');
    std::size_t const written =
        ZSTD_decompress(out.data(), out.size(), compressed.data(), compressed.size());
    if (ZSTD_isError(written)) {
        throw std::runtime_error(std::string("zstd decompression failed: ") + ZSTD_getErrorName(written));
    }
    out.resize(written);
    return out;
}

// chronicle::io::zstd_codec() -- pass to SessionWriter's/load_session's
// `CompressionCodec const*`/codec-list parameters (format.hpp) to opt a
// specific writer/reader into Zstd, without format.hpp, session_writer.hpp,
// or loaded_session.hpp themselves ever depending on zstd.h.
[[nodiscard]] inline CompressionCodec const& zstd_codec() {
    static constexpr CompressionCodec codec{CompressionKind::Zstd, &zstd_compress, &zstd_decompress};
    return codec;
}

} // namespace chronicle::io
