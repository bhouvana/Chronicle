// CHRONICLE_TRACK_TYPE / TrackedType<T,...>: docs/07-api-design.md's
// "registration without wrapper types" path for ABI-sensitive/POD structs,
// implemented via an explicit handle rather than the sketch's address-keyed
// lookup -- see docs/adr/0011-tracked-type-explicit-handle.md.

#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

#include <string>

using chronicle::Session;

namespace {

struct NetworkPacket {
    int seq = 0;
    float latency = 0.0f;
};

} // namespace

CHRONICLE_TRACK_TYPE(NetworkPacket, seq, latency);

namespace {

struct SingleFieldStruct {
    double value = 0.0;
};

} // namespace

CHRONICLE_TRACK_TYPE(SingleFieldStruct, value);

CHRONICLE_TEST(track_type_preserves_pod_layout) {
    // The whole point of this path: NetworkPacket must have exactly the
    // layout a plain, untouched struct with the same fields would -- no
    // hidden Stream pointer embedded in it the way tracked<T> would need.
    struct PlainEquivalent {
        int seq;
        float latency;
    };
    CHRONICLE_CHECK(sizeof(NetworkPacket) == sizeof(PlainEquivalent));
    CHRONICLE_CHECK(alignof(NetworkPacket) == alignof(PlainEquivalent));
}

CHRONICLE_TEST(track_type_records_initial_values_as_version_zero) {
    Session session;
    NetworkPacket p{7, 1.5f};
    auto tracked = chronicle::track_type(p, session, "packet");

    CHRONICLE_CHECK(tracked.get<0>() == 7);
    CHRONICLE_CHECK(tracked.get<1>() == 1.5f);

    auto hx0 = tracked.stream<0>().history();
    auto hx1 = tracked.stream<1>().history();
    CHRONICLE_CHECK(hx0.size() == 1);
    CHRONICLE_CHECK(hx0.front().value == 7);
    CHRONICLE_CHECK(hx1.size() == 1);
    CHRONICLE_CHECK(hx1.front().value == 1.5f);
}

CHRONICLE_TEST(track_type_set_writes_the_field_and_records_it) {
    Session session;
    NetworkPacket p{};
    auto tracked = chronicle::track_type(p, session, "packet");

    tracked.set<0>(42);
    tracked.set<1>(3.25f);

    // The underlying struct is genuinely mutated, not just the recording.
    CHRONICLE_CHECK(p.seq == 42);
    CHRONICLE_CHECK(p.latency == 3.25f);
    CHRONICLE_CHECK(tracked.get<0>() == 42);

    auto hx0 = tracked.stream<0>().history();
    CHRONICLE_CHECK(hx0.size() == 2); // initial 0, then set to 42
    CHRONICLE_CHECK(hx0.back().value == 42);
    CHRONICLE_CHECK(chronicle::is_known(hx0.back().call_site)); // set<I>() captures its caller
}

CHRONICLE_TEST(track_type_stream_names_are_dotted_with_field_names) {
    // Verified indirectly: create_stream() names must not collide across a
    // struct's own fields (name + "." + fieldName, per detail::
    // make_tracked_type() in tracked_type.hpp), or the second field's
    // stream would alias the first's -- both fields' history sizes staying
    // independent after mutating both is the observable proof.
    Session session;
    NetworkPacket p{};
    auto tracked = chronicle::track_type(p, session, "packet");
    tracked.set<0>(1);
    tracked.set<1>(2.0f);
    CHRONICLE_CHECK(tracked.stream<0>().history().size() == 2);
    CHRONICLE_CHECK(tracked.stream<1>().history().size() == 2);
}

CHRONICLE_TEST(track_type_works_for_a_single_field_struct) {
    Session session;
    SingleFieldStruct s{2.5};
    auto tracked = chronicle::track_type(s, session, "single");

    CHRONICLE_CHECK(tracked.get<0>() == 2.5);
    tracked.set<0>(9.75);
    CHRONICLE_CHECK(s.value == 9.75);
    CHRONICLE_CHECK(tracked.stream<0>().history().size() == 2);
}
