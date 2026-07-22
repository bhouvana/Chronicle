// Zstd at-rest compression (docs/adr/0014-storage-engine-compression.md).
// Only built when Zstd's CMake package is found (tests/unit/CMakeLists.txt)
// -- these are the only tests in this suite with an external dependency,
// same reasoning as tools/codegen and the Tracy example being opt-in.

#include "chronicle/chronicle.hpp"
#include "chronicle/io/loaded_session.hpp"
#include "chronicle/io/session_writer.hpp"
#include "chronicle/io/zstd_codec.hpp"
#include "test_framework.hpp"

#include <array>
#include <sstream>
#include <string>

using namespace chronicle;
using namespace chronicle::io;

CHRONICLE_TEST(zstd_codec_round_trips_arbitrary_bytes) {
    std::string const original = "the quick brown fox jumps over the lazy dog, repeated: "
                                  "the quick brown fox jumps over the lazy dog";
    std::string const compressed = zstd_compress(original);
    std::string const decompressed = zstd_decompress(compressed);
    CHRONICLE_CHECK(decompressed == original);
}

CHRONICLE_TEST(zstd_codec_round_trips_empty_input) {
    std::string const compressed = zstd_compress("");
    std::string const decompressed = zstd_decompress(compressed);
    CHRONICLE_CHECK(decompressed.empty());
}

CHRONICLE_TEST(session_writer_with_zstd_codec_round_trips_scalar_stream) {
    Session session;
    chronicle::tracked<int> health{100};
    chronicle::track(health, session, "player.health");
    health = 75;
    health = 50;

    std::stringstream ss;
    {
        SessionWriter writer(ss, session, &zstd_codec());
        writer.write(health);
    }

    std::array<CompressionCodec, 1> const codecs{zstd_codec()};
    auto const loaded = load_session(ss, codecs);
    auto const* stream = loaded.find("player.health");
    CHRONICLE_CHECK(stream != nullptr);
    CHRONICLE_CHECK(stream->events.size() == 3);
    CHRONICLE_CHECK(stream->events[0].value.i == 100);
    CHRONICLE_CHECK(stream->events[1].value.i == 75);
    CHRONICLE_CHECK(stream->events[2].value.i == 50);
}

CHRONICLE_TEST(session_writer_with_zstd_codec_round_trips_multiple_streams) {
    Session session;
    chronicle::tracked_vector<int> items;
    chronicle::track(items, session, "items");
    items.push_back(1);
    items.push_back(2);
    items.update(0, 10);

    chronicle::tracked_map<std::string, int> scores;
    chronicle::track(scores, session, "scores");
    scores.set("alice", 10);
    scores.erase("alice");

    std::stringstream ss;
    {
        SessionWriter writer(ss, session, &zstd_codec());
        writer.write(items);
        writer.write(scores);
    }

    std::array<CompressionCodec, 1> const codecs{zstd_codec()};
    auto const loaded = load_session(ss, codecs);
    CHRONICLE_CHECK(loaded.streams.size() == 2);
    auto const* items_stream = loaded.find("items");
    CHRONICLE_CHECK(items_stream != nullptr);
    CHRONICLE_CHECK(items_stream->shape == StreamShape::IndexedOp);
    CHRONICLE_CHECK(items_stream->events.size() == 3);
    auto const* scores_stream = loaded.find("scores");
    CHRONICLE_CHECK(scores_stream != nullptr);
    CHRONICLE_CHECK(scores_stream->events.size() == 2);
}

// A compressed file read without the codec that produced it must fail
// clearly, not silently misparse compressed bytes as if they were the
// plain format -- exactly the failure mode format.hpp's CompressionKind
// tag exists to prevent.
CHRONICLE_TEST(load_session_without_matching_codec_throws_a_clear_error) {
    Session session;
    chronicle::tracked<int> health{100};
    chronicle::track(health, session, "player.health");

    std::stringstream ss;
    {
        SessionWriter writer(ss, session, &zstd_codec());
        writer.write(health);
    }

    bool threw = false;
    try {
        [[maybe_unused]] auto const loaded = load_session(ss); // no codecs passed
    } catch (std::exception const&) {
        threw = true;
    }
    CHRONICLE_CHECK(threw);
}

// Uncompressed (CompressionKind::None) files must still round-trip when
// codecs are supplied but unused -- a codec list is "what this reader can
// handle if needed," not "what every file must be."
CHRONICLE_TEST(uncompressed_session_still_round_trips_when_codecs_are_supplied) {
    Session session;
    chronicle::tracked<int> health{100};
    chronicle::track(health, session, "player.health");

    std::stringstream ss;
    {
        SessionWriter writer(ss, session); // no codec -- writes uncompressed
        writer.write(health);
    }

    std::array<CompressionCodec, 1> const codecs{zstd_codec()};
    auto const loaded = load_session(ss, codecs);
    auto const* stream = loaded.find("player.health");
    CHRONICLE_CHECK(stream != nullptr);
    CHRONICLE_CHECK(stream->events.size() == 1);
    CHRONICLE_CHECK(stream->events[0].value.i == 100);
}
