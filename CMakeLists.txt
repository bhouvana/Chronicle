cmake_minimum_required(VERSION 3.20)
project(chronicle
    VERSION 0.1.0
    DESCRIPTION "Time travel for runtime state"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Lets editors (VS Code's cpptools, clangd, ...) resolve include paths and
# the actual flags each target compiles with, instead of guessing --
# this is what the "C/C++ Configuration Warnings" panel is asking for.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CHRONICLE_BUILD_TESTS "Build chronicle-core unit tests" ON)
option(CHRONICLE_BUILD_EXAMPLES "Build chronicle examples" ON)
option(CHRONICLE_BUILD_BENCH "Build chronicle-bench microbenchmark suite" ON)
option(CHRONICLE_BUILD_TOOLS "Build chronicle-cli" ON)
option(CHRONICLE_BUILD_CODEGEN "Build chronicle-codegen (requires a Clang/LLVM CMake package install)" OFF)
option(CHRONICLE_BUILD_TRACY_EXAMPLE "Build the Tracy bridge example (requires a Tracy CMake package install)" OFF)
option(CHRONICLE_BUILD_ENTT_ADAPTER "Build chronicle-adapter-entt and its example (requires an EnTT CMake package install)" OFF)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src)

if(CHRONICLE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
endif()

if(CHRONICLE_BUILD_EXAMPLES)
    add_subdirectory(examples/minimal)
    add_subdirectory(examples/container)
    add_subdirectory(examples/map)
    add_subdirectory(examples/export)
endif()

if(CHRONICLE_BUILD_BENCH)
    add_subdirectory(bench)
endif()

if(CHRONICLE_BUILD_TOOLS)
    add_subdirectory(tools/cli)
endif()

if(CHRONICLE_BUILD_CODEGEN)
    add_subdirectory(tools/codegen)
endif()

if(CHRONICLE_BUILD_TRACY_EXAMPLE)
    add_subdirectory(examples/tracy)
endif()

if(CHRONICLE_BUILD_ENTT_ADAPTER)
    add_subdirectory(adapters/entt)
    # adapters/entt/CMakeLists.txt returns early (no chronicle-adapter-entt
    # target) when EnTT's CMake package isn't found -- only build the
    # example when the adapter itself actually configured.
    if(CHRONICLE_BUILD_EXAMPLES AND TARGET chronicle-adapter-entt)
        add_subdirectory(examples/entt-integration)
    endif()
endif()
