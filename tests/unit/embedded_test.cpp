#include <cstdio>
// docs/12-future-research-topics.md topic 7, docs/adr/0027-embedded-tier.md:
// chronicle::embedded::TrackedScalar<T, Capacity> -- fixed-capacity,
// allocation-free circular history. The allocation-free claim is verified
// directly here (global operator new/delete overrides + a counter), not
// assumed from reading the code, same bar every other feature in this
// project holds itself to (see e.g. ADR 0013's Tracy bridge cost
// measurement).

#include "chronicle/embedded.hpp"
#include "test_framework.hpp"

#include <cstdlib>
#include <new>

namespace {
std::size_t g_heap_alloc_count = 0;
} // namespace

void* operator new(std::size_t size) {
    ++g_heap_alloc_count;
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using chronicle::embedded::TrackedScalar;

CHRONICLE_TEST(tracked_scalar_reads_back_current_value) {
    TrackedScalar<int, 4> field{10};
    CHRONICLE_CHECK(field.get() == 10);
    field = 20;
    CHRONICLE_CHECK(field.get() == 20);
    int const as_int = field; // implicit conversion
    CHRONICLE_CHECK(as_int == 20);
}

CHRONICLE_TEST(tracked_scalar_tracks_total_recorded_and_size_before_wrap) {
    TrackedScalar<int, 4> field{1};
    field = 2;
    field = 3;
    CHRONICLE_CHECK(field.total_recorded() == 3);
    CHRONICLE_CHECK(field.size() == 3);
    CHRONICLE_CHECK(field.capacity() == 4);
    CHRONICLE_CHECK(field[0] == 1);
    CHRONICLE_CHECK(field[1] == 2);
    CHRONICLE_CHECK(field[2] == 3);
}

CHRONICLE_TEST(tracked_scalar_evicts_oldest_after_wrap) {
    TrackedScalar<int, 3> field{1};
    field = 2;
    field = 3;
    field = 4; // evicts 1
    field = 5; // evicts 2
    CHRONICLE_CHECK(field.total_recorded() == 5);
    CHRONICLE_CHECK(field.size() == 3); // capped at capacity
    CHRONICLE_CHECK(field[0] == 3);
    CHRONICLE_CHECK(field[1] == 4);
    CHRONICLE_CHECK(field[2] == 5);
    CHRONICLE_CHECK(field.get() == 5);
}

CHRONICLE_TEST(tracked_scalar_never_touches_the_heap) {
    std::size_t const before = g_heap_alloc_count;
    {
        TrackedScalar<double, 64> field{0.0};
        for (int i = 0; i < 1000; ++i) {
            field = static_cast<double>(i);
        }
        [[maybe_unused]] auto const s = field.size();
        [[maybe_unused]] auto const v = field[0];
    }
    // Snapshot into a plain local *before* CHRONICLE_CHECK runs -- found by
    // running this exact test, not by inspection: CHRONICLE_CHECK's own
    // record_check() takes its test-name/expr/file arguments as
    // std::string const&, so passing the __func__/#expr/__FILE__ literals
    // implicitly constructs temporary std::strings (both long enough to
    // exceed SSO) as part of *every* CHRONICLE_CHECK call, allocating on
    // the heap regardless of whether the check passes. Comparing
    // g_heap_alloc_count directly inside the CHECK expression is a real
    // hazard: C++ doesn't sequence a function call's argument
    // constructions relative to each other, so the diagnostic-string
    // allocation can happen before the equality is evaluated, making the
    // check see its own overhead. Snapshotting first removes the ambiguity
    // -- the comparison then only ever sees plain, already-fixed integers.
    bool const no_new_allocations = (g_heap_alloc_count == before);
    CHRONICLE_CHECK(no_new_allocations);
}
