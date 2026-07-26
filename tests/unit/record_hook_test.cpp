// docs/adr/0040-composable-record-hooks.md: Stream<T> now supports up to
// kMaxRecordHooks (4) independently-attached RecordHooks, superseding
// ADR 0013's original "only one hook at a time" -- real evidence since
// (Tracy + derive() both needing the same stream's hook slot) justified
// revisiting it. set_record_hook() keeps its exact old signature and
// single-hook-overwrite behavior (ADR 0018 API stability), reimplemented
// as sugar over the new slot mechanism.

#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

#include <vector>

using namespace chronicle;

namespace {
void record_int(std::vector<int>* log, int const& value) { log->push_back(value); }

void hook_a(void* ctx, int const& value, std::source_location const&) {
    record_int(static_cast<std::vector<int>*>(ctx), value);
}
void hook_b(void* ctx, int const& value, std::source_location const&) {
    record_int(static_cast<std::vector<int>*>(ctx), value * 100);
}
} // namespace

CHRONICLE_TEST(add_record_hook_lets_two_independent_hooks_both_fire) {
    Session session;
    tracked<int> value{0};
    track(value, session, "field");
    auto* stream = value.stream();

    std::vector<int> log_a;
    std::vector<int> log_b;
    auto const handle_a = stream->add_record_hook(&hook_a, &log_a);
    auto const handle_b = stream->add_record_hook(&hook_b, &log_b);

    value = 5;

    CHRONICLE_CHECK(log_a.size() == 1 && log_a.back() == 5);
    CHRONICLE_CHECK(log_b.size() == 1 && log_b.back() == 500);

    stream->remove_record_hook(handle_a);
    stream->remove_record_hook(handle_b);
}

CHRONICLE_TEST(remove_record_hook_leaves_the_other_hook_firing) {
    Session session;
    tracked<int> value{0};
    track(value, session, "field");
    auto* stream = value.stream();

    std::vector<int> log_a;
    std::vector<int> log_b;
    auto const handle_a = stream->add_record_hook(&hook_a, &log_a);
    auto const handle_b = stream->add_record_hook(&hook_b, &log_b);

    stream->remove_record_hook(handle_a);
    value = 7;

    CHRONICLE_CHECK(log_a.empty());              // detached -- must not fire
    CHRONICLE_CHECK(log_b.size() == 1 && log_b.back() == 700); // still attached

    stream->remove_record_hook(handle_b);
}

// Real backward-compatibility check, not assumed: any code built against
// the pre-ADR-0040 single-hook API must keep working unchanged.
CHRONICLE_TEST(set_record_hook_keeps_its_old_single_hook_overwrite_behavior) {
    Session session;
    tracked<int> value{0};
    track(value, session, "field");
    auto* stream = value.stream();

    std::vector<int> log_first;
    std::vector<int> log_second;
    stream->set_record_hook(&hook_a, &log_first);
    value = 1;
    CHRONICLE_CHECK(log_first.size() == 1);

    stream->set_record_hook(&hook_a, &log_second); // must replace, not add
    value = 2;
    CHRONICLE_CHECK(log_first.size() == 1); // unchanged -- old hook really detached
    CHRONICLE_CHECK(log_second.size() == 1 && log_second.back() == 2);

    stream->set_record_hook(nullptr, nullptr); // detach everything
    value = 3;
    CHRONICLE_CHECK(log_second.size() == 1); // unchanged
}

CHRONICLE_TEST(add_record_hook_throws_once_all_slots_are_full) {
    Session session;
    tracked<int> value{0};
    track(value, session, "field");
    auto* stream = value.stream();

    std::vector<int> sink;
    for (std::size_t i = 0; i < Stream<int>::kMaxRecordHooks; ++i) {
        stream->add_record_hook(&hook_a, &sink);
    }

    bool threw = false;
    try {
        stream->add_record_hook(&hook_a, &sink);
    } catch (std::runtime_error const&) {
        threw = true;
    }
    CHRONICLE_CHECK(threw);
}
