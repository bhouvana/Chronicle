// docs/adr/0021-pmr-allocator-adapter.md. Uses real std::pmr containers
// (std::pmr::vector, std::pmr::string) driven through
// chronicle::TrackedMemoryResource -- not a mock allocator -- to confirm
// real allocate/deallocate calls produce the expected Insert/Erase MapOp
// events.

#include "chronicle/chronicle.hpp"
#include "test_framework.hpp"

#include <memory_resource>
#include <vector>

using namespace chronicle;

CHRONICLE_TEST(allocation_records_insert_with_the_real_address_and_size) {
    Session session;
    TrackedMemoryResource arena(std::pmr::get_default_resource(), session, "arena");

    {
        std::pmr::vector<int> v(&arena);
        v.reserve(16); // one real allocation through `arena`

        auto const hx = arena.stream().history();
        CHRONICLE_CHECK(hx.size() == 1);
        CHRONICLE_CHECK(hx.front().value.kind == ContainerOpKind::Insert);
        CHRONICLE_CHECK(hx.front().value.value == 16 * sizeof(int));
        CHRONICLE_CHECK(hx.front().value.key != 0); // a real heap address
    }
}

CHRONICLE_TEST(deallocation_records_erase_for_the_same_address) {
    Session session;
    TrackedMemoryResource arena(std::pmr::get_default_resource(), session, "arena");

    {
        std::pmr::vector<int> v(&arena);
        v.reserve(16);
    } // v's destructor deallocates here

    auto const hx = arena.stream().history();
    CHRONICLE_CHECK(hx.size() == 2);
    CHRONICLE_CHECK(hx[0].value.kind == ContainerOpKind::Insert);
    CHRONICLE_CHECK(hx[1].value.kind == ContainerOpKind::Erase);
    CHRONICLE_CHECK(hx[0].value.key == hx[1].value.key); // same address, freed
}

CHRONICLE_TEST(multiple_allocations_are_tracked_independently_by_address) {
    Session session;
    TrackedMemoryResource arena(std::pmr::get_default_resource(), session, "arena");

    std::pmr::vector<int> a(&arena);
    a.reserve(4);
    std::pmr::vector<int> b(&arena);
    b.reserve(8);

    auto const hx = arena.stream().history();
    CHRONICLE_CHECK(hx.size() == 2);
    CHRONICLE_CHECK(hx[0].value.key != hx[1].value.key); // distinct addresses
    CHRONICLE_CHECK(hx[0].value.value == 4 * sizeof(int));
    CHRONICLE_CHECK(hx[1].value.value == 8 * sizeof(int));
}

CHRONICLE_TEST(reallocation_on_growth_records_erase_of_old_address_and_insert_of_new) {
    Session session;
    TrackedMemoryResource arena(std::pmr::get_default_resource(), session, "arena");

    std::pmr::vector<int> v(&arena);
    v.reserve(4);
    auto const first_hx = arena.stream().history();
    auto const first_address = first_hx.front().value.key;

    v.reserve(1024); // forces reallocation: old buffer freed, new one allocated

    auto const hx = arena.stream().history();
    CHRONICLE_CHECK(hx.size() >= 3); // insert, erase(old), insert(new) at minimum
    bool found_erase_of_first = false;
    for (auto const& record : hx) {
        if (record.value.kind == ContainerOpKind::Erase && record.value.key == first_address) {
            found_erase_of_first = true;
        }
    }
    CHRONICLE_CHECK(found_erase_of_first);
}
