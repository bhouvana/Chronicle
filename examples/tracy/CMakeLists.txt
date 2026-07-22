# Like tools/codegen, this example links an external dependency
# (Tracy::TracyClient) most consumers won't have installed, so it's opt-in
# and gracefully skips rather than failing the whole project's configure
# when Tracy isn't available. See docs/adr/0013-tracy-bridge.md.
find_package(Tracy CONFIG QUIET)

if(NOT Tracy_FOUND)
    message(STATUS "chronicle-example-tracy: Tracy CMake package not found, skipping (set CMAKE_PREFIX_PATH to a Tracy install to enable)")
    return()
endif()

add_executable(chronicle-example-tracy main.cpp)
target_link_libraries(chronicle-example-tracy PRIVATE chronicle::core Tracy::TracyClient)
