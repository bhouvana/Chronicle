// docs/adr/0021-pmr-allocator-adapter.md: real std::pmr allocations
// recorded through chronicle::TrackedMemoryResource, written to
// demo_allocator.chronicle for chronicle-cli to read back -- reusing the
// exact MapOp/KeyedOp wire shape tracked_map<K,V> already produces, so no
// chronicle-cli/viewer changes were needed for this adapter either (same
// payoff ADR 0015 already got for the EnTT adapter). Run this, then e.g.:
//
//   chronicle-cli list demo_allocator.chronicle
//   chronicle-cli history demo_allocator.chronicle arena

#include <chronicle/chronicle.hpp>
#include <chronicle/io/session_writer.hpp>

#include <fstream>
#include <iostream>
#include <memory_resource>
#include <vector>

int main() {
    chronicle::Session session;
    chronicle::TrackedMemoryResource arena(std::pmr::get_default_resource(), session, "arena");

    {
        std::pmr::vector<int> a(&arena);
        a.reserve(4);
        std::pmr::vector<int> b(&arena);
        b.reserve(8);
        a.reserve(256); // forces reallocation: a real free + a real new allocation
    } // both vectors' destructors deallocate here

    std::ofstream out("demo_allocator.chronicle", std::ios::binary);
    if (!out) {
        std::cerr << "failed to open demo_allocator.chronicle for writing\n";
        return 1;
    }

    // Reuses tracked_map<K,V>'s own serialization path -- session_writer.hpp
    // only needs a bound Stream<MapOp<K,V>>&, which TrackedMemoryResource
    // already exposes via stream().
    chronicle::tracked_map<std::uint64_t, std::uint64_t> shell;
    shell.bind(arena.stream());
    chronicle::io::SessionWriter writer(out, session);
    writer.write(shell);

    std::cout << "wrote demo_allocator.chronicle (" << arena.stream().history().size()
              << " allocation event(s))\n";
    return 0;
}
