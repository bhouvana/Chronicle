# chronicle-cli links only chronicle-core's io headers (loaded_session.hpp),
# never the target program's own types -- see main.cpp's header comment and
# docs/adr/0005-cli-requires-on-disk-format.md.
add_executable(chronicle-cli main.cpp html_export.cpp)
target_link_libraries(chronicle-cli PRIVATE chronicle::core)

# Transparent Zstd-compressed-file support (docs/adr/0014-storage-engine-
# compression.md) is a build-time capability, not a required dependency:
# most environments building chronicle-cli (the default CHRONICLE_BUILD_TOOLS
# target) won't have Zstd's CMake package installed, and chronicle-cli must
# keep building for them -- it just won't be able to read compressed
# session files without this. When Zstd *is* available, no extra flag is
# needed at runtime: main.cpp's CHRONICLE_CLI_HAVE_ZSTD guard picks it up
# automatically.
find_package(zstd CONFIG QUIET)
if(zstd_FOUND)
    target_compile_definitions(chronicle-cli PRIVATE CHRONICLE_CLI_HAVE_ZSTD)
    target_link_libraries(chronicle-cli PRIVATE zstd::libzstd)
else()
    message(STATUS "chronicle-cli: Zstd CMake package not found, building without compressed-file support")
endif()

# `chronicle-cli serve` (docs/adr/0016-interactive-browser-viewer.md) is
# likewise a build-time capability, not a required dependency -- same
# opt-in reasoning as Zstd above. serve.cpp (which #includes <httplib.h>)
# is only compiled into this target when cpp-httplib's CMake package is
# found; main.cpp's CHRONICLE_CLI_HAVE_HTTPLIB guard hides the `serve`
# subcommand entirely otherwise, rather than exposing a subcommand that
# would immediately fail.
find_package(httplib CONFIG QUIET)
if(httplib_FOUND)
    target_sources(chronicle-cli PRIVATE serve.cpp)
    target_compile_definitions(chronicle-cli PRIVATE CHRONICLE_CLI_HAVE_HTTPLIB)
    target_link_libraries(chronicle-cli PRIVATE httplib::httplib)
else()
    message(STATUS "chronicle-cli: cpp-httplib CMake package not found, building without the 'serve' subcommand")
endif()
