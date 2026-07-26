// docs/12-future-research-topics.md topic 2, docs/adr/0030-p2996-reflection-gate.md.
// This is a deliberate canary, not a normal correctness test: it asserts
// today's real, verified state (no compiler in this project's CI matrix
// defines __cpp_reflection) and is *meant* to fail the day that stops
// being true -- a build break that correctly signals "a real P2996-based
// registration backend can finally be implemented and verified," rather
// than this gap staying silently stale forever.

#include "chronicle/reflect_p2996.hpp"
#include "test_framework.hpp"

CHRONICLE_TEST(p2996_reflection_is_not_yet_available_on_any_ci_compiler) {
    CHRONICLE_CHECK(chronicle::reflect::p2996_available == false);
}
