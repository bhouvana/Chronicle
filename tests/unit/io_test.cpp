#include "chronicle/chronicle.hpp"
#include "chronicle/io/loaded_session.hpp"
#include "chronicle/io/session_writer.hpp"
#include "test_framework.hpp"

#include <sstream>
#include <string>

using namespace chronicle;
using namespace chronicle::io;

CHRONICLE_TEST(wire_codec_round_trips_int_double_bool_string) {
    std::stringstream ss;
    WireCodec<int>::write(ss, -42);
    WireCodec<double>::write(ss, 3.5);
    WireCodec<bool>::write(ss, true);
    WireCodec<std::string>::write(ss, "hello");

    CHRONICLE_CHECK(WireCodec<int>::read(ss) == -42);
    CHRONICLE_CHECK(WireCodec<double>::read(ss) == 3.5);
    CHRONICLE_CHECK(WireCodec<bool>::read(ss) == true);
    CHRONICLE_CHECK(WireCodec<std::string>::read(ss) == "hello");
}

CHRONICLE_TEST(session_writer_and_load_session_round_trip_scalar_stream) {
    Session session;
    chronicle::tracked<int> health{100};
    chronicle::track(health, session, "player.health");
    health = 75;
    health = 50;

    std::stringstream ss;
    {
        SessionWriter writer(ss, session);
        writer.write(health);
    }

    auto const loaded = load_session(ss);
    auto const* stream = loaded.find("player.health");
    CHRONICLE_CHECK(stream != nullptr);
    CHRONICLE_CHECK(stream->shape == StreamShape::Scalar);
    CHRONICLE_CHECK(stream->events.size() == 3);
    CHRONICLE_CHECK(stream->events[0].value.kind == WireKind::Int64);
    CHRONICLE_CHECK(stream->events[0].value.i == 100);
    CHRONICLE_CHECK(stream->events[1].value.i == 75);
    CHRONICLE_CHECK(stream->events[2].value.i == 50);
    CHRONICLE_CHECK(stream->events[0].version == 0);
    CHRONICLE_CHECK(stream->events[2].version == 2);
}

CHRONICLE_TEST(session_writer_and_load_session_round_trip_vector_stream) {
    Session session;
    chronicle::tracked_vector<int> items;
    chronicle::track(items, session, "items");
    items.push_back(1);
    items.push_back(2);
    items.update(0, 10);
    items.erase(1);

    std::stringstream ss;
    {
        SessionWriter writer(ss, session);
        writer.write(items);
    }

    auto const loaded = load_session(ss);
    auto const* stream = loaded.find("items");
    CHRONICLE_CHECK(stream != nullptr);
    CHRONICLE_CHECK(stream->shape == StreamShape::IndexedOp);
    CHRONICLE_CHECK(stream->events.size() == 4);
    CHRONICLE_CHECK(stream->events[0].op_kind == ContainerOpKind::Insert);
    CHRONICLE_CHECK(stream->events[0].key_or_index.u == 0);
    CHRONICLE_CHECK(stream->events[0].value.i == 1);
    CHRONICLE_CHECK(stream->events[2].op_kind == ContainerOpKind::Update);
    CHRONICLE_CHECK(stream->events[2].key_or_index.u == 0);
    CHRONICLE_CHECK(stream->events[2].value.i == 10);
    CHRONICLE_CHECK(stream->events[3].op_kind == ContainerOpKind::Erase);
}

CHRONICLE_TEST(session_writer_and_load_session_round_trip_map_stream) {
    Session session;
    chronicle::tracked_map<std::string, int> scores;
    chronicle::track(scores, session, "scores");
    scores.set("alice", 10);
    scores.set("alice", 15);
    scores.erase("alice");

    std::stringstream ss;
    {
        SessionWriter writer(ss, session);
        writer.write(scores);
    }

    auto const loaded = load_session(ss);
    auto const* stream = loaded.find("scores");
    CHRONICLE_CHECK(stream != nullptr);
    CHRONICLE_CHECK(stream->shape == StreamShape::KeyedOp);
    CHRONICLE_CHECK(stream->events.size() == 3);
    CHRONICLE_CHECK(stream->events[0].op_kind == ContainerOpKind::Insert);
    CHRONICLE_CHECK(stream->events[0].key_or_index.kind == WireKind::String);
    CHRONICLE_CHECK(stream->events[0].key_or_index.s == "alice");
    CHRONICLE_CHECK(stream->events[0].value.i == 10);
    CHRONICLE_CHECK(stream->events[1].op_kind == ContainerOpKind::Update);
    CHRONICLE_CHECK(stream->events[1].value.i == 15);
    CHRONICLE_CHECK(stream->events[2].op_kind == ContainerOpKind::Erase);
}

CHRONICLE_TEST(session_writer_saves_multiple_streams_in_one_file) {
    Session session;
    chronicle::tracked<int> a{1};
    chronicle::track(a, session, "a");
    chronicle::tracked<int> b{2};
    chronicle::track(b, session, "b");

    std::stringstream ss;
    {
        SessionWriter writer(ss, session);
        writer.write(a);
        writer.write(b);
    }

    auto const loaded = load_session(ss);
    CHRONICLE_CHECK(loaded.streams.size() == 2);
    CHRONICLE_CHECK(loaded.find("a") != nullptr);
    CHRONICLE_CHECK(loaded.find("b") != nullptr);
}

CHRONICLE_TEST(load_session_rejects_files_with_bad_magic) {
    std::stringstream ss;
    ss << "NOPE1234";
    bool threw = false;
    try {
        [[maybe_unused]] auto const loaded = load_session(ss);
    } catch (std::exception const&) {
        threw = true;
    }
    CHRONICLE_CHECK(threw);
}

// docs/adr/0010-call-site-capture.md / format v2: call_site round-trips
// through the on-disk format, and known()/unknown() survives correctly for
// both plain assignment (unknown) and chronicle::set() (known).
CHRONICLE_TEST(call_site_round_trips_through_on_disk_format) {
    Session session;
    chronicle::tracked<int> health{100}; // this line: track()'s call site
    chronicle::track(health, session, "player.health");
    health = 75;              // plain assignment: no call site
    chronicle::set(health, 50); // chronicle::set(): real call site

    std::stringstream ss;
    {
        SessionWriter writer(ss, session);
        writer.write(health);
    }

    auto const loaded = load_session(ss);
    auto const* stream = loaded.find("player.health");
    CHRONICLE_CHECK(stream != nullptr);
    CHRONICLE_CHECK(stream->events.size() == 3);

    CHRONICLE_CHECK(stream->events[0].call_site.known()); // from track()
    CHRONICLE_CHECK(stream->events[0].call_site.file.find("io_test.cpp") != std::string::npos);

    CHRONICLE_CHECK(!stream->events[1].call_site.known()); // from `health = 75`

    CHRONICLE_CHECK(stream->events[2].call_site.known()); // from chronicle::set()
    CHRONICLE_CHECK(stream->events[2].call_site.file.find("io_test.cpp") != std::string::npos);
    CHRONICLE_CHECK(stream->events[2].call_site.line > 0);
}

// docs/adr/0019-hybrid-logical-clock.md / format v4: hlc round-trips
// through the on-disk format -- unknown when the producing Session never
// enabled causal_clock, known and strictly increasing when it did.
CHRONICLE_TEST(hlc_round_trips_through_on_disk_format_when_disabled) {
    Session session; // causal_clock defaults to false
    chronicle::tracked<int> health{100};
    chronicle::track(health, session, "player.health");
    health = 75;

    std::stringstream ss;
    {
        SessionWriter writer(ss, session);
        writer.write(health);
    }

    auto const loaded = load_session(ss);
    auto const* stream = loaded.find("player.health");
    CHRONICLE_CHECK(stream != nullptr);
    for (auto const& event : stream->events) {
        CHRONICLE_CHECK(!is_known(event.hlc));
    }
}

CHRONICLE_TEST(hlc_round_trips_through_on_disk_format_when_enabled) {
    Session session(Session::Config{RetentionPolicy::ring_window(1024), OverflowPolicy::DropOldest, 64, true});
    chronicle::tracked<int> health{100};
    chronicle::track(health, session, "player.health");
    health = 75;
    health = 50;

    std::stringstream ss;
    {
        SessionWriter writer(ss, session);
        writer.write(health);
    }

    auto const loaded = load_session(ss);
    auto const* stream = loaded.find("player.health");
    CHRONICLE_CHECK(stream != nullptr);
    CHRONICLE_CHECK(stream->events.size() == 3);
    CHRONICLE_CHECK(is_known(stream->events[0].hlc));
    CHRONICLE_CHECK(is_known(stream->events[1].hlc));
    CHRONICLE_CHECK(is_known(stream->events[2].hlc));
    CHRONICLE_CHECK(stream->events[0].hlc < stream->events[1].hlc);
    CHRONICLE_CHECK(stream->events[1].hlc < stream->events[2].hlc);
}
