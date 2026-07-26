// docs/12-future-research-topics.md topic 3, docs/adr/0029-memcpy-interposition.md.
// The address-range registry itself needs no Detours/platform hook to
// test -- only tools/memcpy-shim's actual hook does (verified separately,
// manually, since it requires linking a real Detours-hooked process; see
// the ADR for that verification).

#include "chronicle/chronicle.hpp"
#include "chronicle/interposition_registry.hpp"
#include "test_framework.hpp"

using namespace chronicle::interposition;

CHRONICLE_TEST(registry_reports_no_overlap_when_nothing_watched) {
    Registry registry;
    int value = 42;
    CHRONICLE_CHECK(!registry.overlaps(&value, sizeof(value)));
}

CHRONICLE_TEST(registry_detects_overlap_within_a_watched_range) {
    Registry registry;
    alignas(16) unsigned char buffer[64] = {};
    registry.watch(buffer, sizeof(buffer));
    CHRONICLE_CHECK(registry.overlaps(buffer, 4));
    CHRONICLE_CHECK(registry.overlaps(buffer + 60, 4));   // tail of the range
    CHRONICLE_CHECK(registry.overlaps(buffer + 32, 100)); // partial overlap past the end
    CHRONICLE_CHECK(!registry.overlaps(buffer + 64, 4));  // strictly past the end
}

CHRONICLE_TEST(registry_stops_reporting_after_unwatch) {
    Registry registry;
    int value = 7;
    registry.watch(&value, sizeof(value));
    CHRONICLE_CHECK(registry.overlaps(&value, sizeof(value)));
    registry.unwatch(&value);
    CHRONICLE_CHECK(!registry.overlaps(&value, sizeof(value)));
}

CHRONICLE_TEST(watch_helper_watches_a_tracked_fields_backing_storage) {
    Registry registry; // a fresh local Registry -- watch()/unwatch() below use Registry::instance()
    chronicle::Session session;
    chronicle::tracked<int> field{5};
    chronicle::track(field, session, "field");

    chronicle::interposition::watch(field);
    CHRONICLE_CHECK(Registry::instance().overlaps(&field.get(), sizeof(int)));
    chronicle::interposition::unwatch(field);
    CHRONICLE_CHECK(!Registry::instance().overlaps(&field.get(), sizeof(int)));
    (void)registry; // unused: instance() is a process-wide singleton, not this local
}
