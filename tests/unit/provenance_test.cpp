// docs/13-vision.md Layer 3, docs/adr/0032-provenance-stacktrace.md.
// chronicle::set_with_stacktrace()/provenance_of(): a full call-chain
// snapshot, beyond last_writer()'s single frame. noinline helper
// functions keep this test's own frames stable regardless of incidental
// compiler inlining decisions (the real, documented limitation this
// feature has -- see the ADR).

#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

using namespace chronicle;

namespace {

#if defined(_MSC_VER)
#define CHRONICLE_TEST_NOINLINE __declspec(noinline)
#else
#define CHRONICLE_TEST_NOINLINE __attribute__((noinline))
#endif

CHRONICLE_TEST_NOINLINE void inner_write(tracked<int>& field, int value) {
    set_with_stacktrace(field, value);
}
CHRONICLE_TEST_NOINLINE void middle_write(tracked<int>& field, int value) {
    inner_write(field, value);
}

} // namespace

CHRONICLE_TEST(provenance_of_returns_nullopt_before_any_stacktrace_write) {
    Session session;
    tracked<int> value{0};
    track(value, session, "field");
    CHRONICLE_CHECK(!provenance_of(value).has_value());
}

CHRONICLE_TEST(set_with_stacktrace_records_a_real_multi_frame_trace) {
    Session session;
    tracked<int> value{0};
    track(value, session, "field");

    middle_write(value, 42);

    CHRONICLE_CHECK(value.get() == 42); // the value itself was still recorded normally
    auto const trace = provenance_of(value);
    CHRONICLE_CHECK(trace.has_value());
    CHRONICLE_CHECK(trace->size() >= 2); // at least inner_write + middle_write frames survive
    // Top frame should be inner_write() itself, in this test file.
    CHRONICLE_CHECK(trace->front().source_line > 0);
}

CHRONICLE_TEST(provenance_of_returns_nullopt_for_an_unrecorded_version) {
    Session session;
    tracked<int> value{0};
    track(value, session, "field");
    middle_write(value, 1); // records provenance at whatever version this lands on
    CHRONICLE_CHECK(!provenance_of(value, 999999).has_value());
}

CHRONICLE_TEST(provenance_does_not_cross_contaminate_between_fields_with_colliding_versions) {
    Session session_a;
    Session session_b;
    tracked<int> a{0};
    tracked<int> b{0};
    track(a, session_a, "a");
    track(b, session_b, "b");

    middle_write(a, 111); // both fields land on the same version number (1)
    set_with_stacktrace(b, 222);

    CHRONICLE_CHECK(current_version(a) == current_version(b)); // colliding version numbers, on purpose
    auto const trace_a = provenance_of(a);
    auto const trace_b = provenance_of(b);
    CHRONICLE_CHECK(trace_a.has_value());
    CHRONICLE_CHECK(trace_b.has_value());
    // trace_a went through two extra real (noinline) frames --
    // middle_write() and inner_write() -- that trace_b's direct call
    // skipped; capture_current_stacktrace()/set_with_stacktrace()'s own
    // frames are identical in both (same instantiated template function
    // either way), so a plain front()-only comparison would NOT
    // disambiguate them -- found by actually running this test, not
    // assumed. Comparing frame count is the robust signal that the
    // (stream_id, version) key really kept the two traces separate rather
    // than one leaking into the other.
    CHRONICLE_CHECK(trace_a->size() > trace_b->size());
}
