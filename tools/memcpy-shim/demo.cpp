// Real, running demonstration of address-range-filtered memcpy
// interposition (docs/12 topic 3, docs/adr/0029-memcpy-interposition.md),
// not just source-level plausibility -- same verification bar this
// project holds every feature to (e.g. ADR 0013's Tracy bridge, verified
// via a real tracy-capture run).
//
// Three cases, exercising both this feature's real capability and its
// inherited, unfixable limitation:
//  1. A compile-time-constant-size memcpy into a WATCHED field's storage.
//     Expected: MSVC /O2 inlines this into raw load/store instructions,
//     emitting zero reference to the memcpy symbol -- the hook cannot
//     fire on something that never calls the function it hooked. This is
//     the structural blind spot the original spike found; address
//     filtering cannot change it, and this case exists specifically to
//     keep confirming that honestly rather than silently dropping the
//     caveat.
//  2. A genuinely runtime-variable-size memcpy (the size is derived from
//     argc, which the optimizer cannot constant-fold) into the same
//     WATCHED field's storage. Expected: this DOES survive as a real,
//     hookable call, and the hook must report it as a watched hit.
//  3. The same runtime-variable-size memcpy into an UNWATCHED buffer.
//     Expected: the hook still fires (total_call_count increments) but
//     must NOT count as a watched hit -- this is the actual address-range
//     filtering capability this module adds over the original,
//     unfiltered spike.

#include <chronicle/chronicle.hpp>
#include <chronicle/interposition_registry.hpp>

#include "shim.hpp"

#include <cstdio>
#include <cstring>

int main(int argc, char**) {
    chronicle::Session session;
    chronicle::tracked<int> watched_field{0};
    chronicle::track(watched_field, session, "watched");
    chronicle::interposition::watch(watched_field);

    if (!chronicle::interposition::install_memcpy_hook()) {
        std::fprintf(stderr, "failed to install memcpy hook\n");
        return 1;
    }

    // Case 1: compile-time-constant size, watched destination.
    int const case1_source = 999;
    std::memcpy(const_cast<int*>(&watched_field.get()), &case1_source, sizeof(int));

    // argc is always >= 1 at runtime but the optimizer cannot prove that
    // at compile time, so this size is genuinely not foldable -- the same
    // technique the original spike used.
    std::size_t const runtime_size = (argc > 100) ? 0 : sizeof(int);

    // Case 2: runtime-variable size, watched destination.
    int const case2_source = 555;
    std::memcpy(const_cast<int*>(&watched_field.get()), &case2_source, runtime_size);

    // Case 3: runtime-variable size, unwatched destination.
    unsigned char unwatched[16] = {};
    unsigned char const unwatched_source[16] = {};
    std::memcpy(unwatched, unwatched_source, runtime_size);

    chronicle::interposition::uninstall_memcpy_hook();

    std::printf("total_calls=%zu watched_hits=%zu final_value=%d\n",
                chronicle::interposition::total_call_count(),
                chronicle::interposition::watched_hit_count(), watched_field.get());
    return 0;
}
