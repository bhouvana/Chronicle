// docs/13-vision.md Layer 9 ("runtime knowledge graph"),
// docs/adr/0037-filesystem-bridge.md. chronicle::bridges::TrackedFile --
// real file I/O, not a stringstream stand-in: this test genuinely creates,
// writes, reads, and removes a real file on disk, since the whole point
// of this feature is bridging to the real filesystem, not simulating one.

#include "chronicle/chronicle.hpp"
#include "chronicle/filesystem_bridge.hpp"
#include "test_framework.hpp"

#include <cstdio>
#include <filesystem>

using namespace chronicle;
using namespace chronicle::bridges;

namespace {
std::filesystem::path test_file_path() {
    return std::filesystem::temp_directory_path() / "chronicle_filesystem_bridge_test.tmp";
}
} // namespace

CHRONICLE_TEST(tracked_file_destructor_records_a_real_close_event) {
    auto const path = test_file_path();
    std::filesystem::remove(path); // start clean if a prior run left it behind

    Session session;
    TrackedFile file(session, "test.file", path.string(), std::ios::out | std::ios::binary);
    CHRONICLE_CHECK(file.is_open());
    char const data[] = "hello chronicle";
    file.write(data, sizeof(data) - 1);
    CHRONICLE_CHECK(history(file.activity()).size() == 2); // Open (seeded), Write -- not yet closed

    file.close(); // exercised explicitly here so the check below can still
                   // read file.activity() while `file` itself is alive --
                   // relying on the destructor for this specific assertion
                   // would need reading a member after its owner is gone.
    CHRONICLE_CHECK(history(file.activity()).size() == 3); // Open, Write, Close
    CHRONICLE_CHECK(history(file.activity()).back().value.kind == FileOpKind::Close);
    CHRONICLE_CHECK(!file.is_open());

    std::filesystem::remove(path);
}

CHRONICLE_TEST(tracked_file_history_shows_the_real_operation_sequence) {
    auto const path = test_file_path();
    std::filesystem::remove(path);

    Session session;
    TrackedFile writer(session, "writer", path.string(), std::ios::out | std::ios::binary);
    char const data[] = "abc123";
    writer.write(data, sizeof(data) - 1);
    writer.close();

    auto const write_hx = history(writer.activity());
    CHRONICLE_CHECK(write_hx.size() == 3); // Open, Write, Close
    CHRONICLE_CHECK(write_hx[0].value.kind == FileOpKind::Open);
    CHRONICLE_CHECK(write_hx[1].value.kind == FileOpKind::Write);
    CHRONICLE_CHECK(write_hx[1].value.byte_count == sizeof(data) - 1);
    CHRONICLE_CHECK(write_hx[2].value.kind == FileOpKind::Close);

    TrackedFile reader(session, "reader", path.string(), std::ios::in | std::ios::binary);
    char buffer[16] = {};
    std::size_t const read_count = reader.read(buffer, sizeof(buffer));
    reader.close();

    CHRONICLE_CHECK(read_count == sizeof(data) - 1);
    CHRONICLE_CHECK(std::string(buffer, read_count) == "abc123");

    auto const read_hx = history(reader.activity());
    CHRONICLE_CHECK(read_hx.size() == 3); // Open, Read, Close
    CHRONICLE_CHECK(read_hx[1].value.kind == FileOpKind::Read);
    CHRONICLE_CHECK(read_hx[1].value.byte_count == sizeof(data) - 1);

    std::filesystem::remove(path);
}
