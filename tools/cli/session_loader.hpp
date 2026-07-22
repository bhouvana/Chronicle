#pragma once

#include <chronicle/io/loaded_session.hpp>
#ifdef CHRONICLE_CLI_HAVE_ZSTD
#include <chronicle/io/zstd_codec.hpp>
#endif

#include <array>
#include <fstream>
#include <stdexcept>
#include <string>

// Shared between main.cpp's file-based subcommands and serve.cpp
// (docs/adr/0016-interactive-browser-viewer.md), which re-reads the file
// fresh on every request rather than once at startup -- both need the
// exact same "open, transparently decompress if built with Zstd, parse"
// logic, so it lives here once instead of twice.

namespace chronicle_cli {

[[nodiscard]] inline chronicle::io::LoadedSession load_file(std::string const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open file: " + path);
    }
#ifdef CHRONICLE_CLI_HAVE_ZSTD
    // Transparent: a session written uncompressed still just works (the
    // codec list is "what this reader can handle if asked," not "what
    // every file must be" -- see loaded_session.hpp), no separate flag for
    // the common case where the file happens not to be compressed.
    std::array<chronicle::io::CompressionCodec, 1> const codecs{chronicle::io::zstd_codec()};
    return chronicle::io::load_session(in, codecs);
#else
    return chronicle::io::load_session(in);
#endif
}

} // namespace chronicle_cli
