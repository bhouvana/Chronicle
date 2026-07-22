# Like tools/codegen, this example links an external dependency
# (Tracy::TracyClient) most consumers won't have installed, so it's opt-in
# and gracefully skips rather than failing the whole project's configure
# when Tracy isn't available. See docs/adr/0013-tracy-bridge.md.
# Tracy_FOUND alone isn't sufficient -- see tests/unit/CMakeLists.txt's
# comment on the analogous Zstd check (docs/adr/0017-ci-performance-gate.md).
find_package(Tracy CONFIG QUIET)

if(NOT Tracy_FOUND OR NOT TARGET Tracy::TracyClient)
    message(STATUS "chronicle-example-tracy: no compatible Tracy CMake package found, skipping (set CMAKE_PREFIX_PATH to a Tracy install to enable)")
    return()
endif()

add_executable(chronicle-example-tracy main.cpp)
target_link_libraries(chronicle-example-tracy PRIVATE chronicle::core Tracy::TracyClient)
