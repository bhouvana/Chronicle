cmake_minimum_required(VERSION 3.20)
project(chronicle
    VERSION 2.1.0
    DESCRIPTION "Time travel for runtime state"
    HOMEPAGE_URL "https://github.com/bhouvana/Chronicle"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Lets editors (VS Code's cpptools, clangd, ...) resolve include paths and
# the actual flags each target compiles with, instead of guessing --
# this is what the "C/C++ Configuration Warnings" panel is asking for.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# True only when Chronicle is the top-level project being configured, not
# when it's pulled in via FetchContent/add_subdirectory by someone else's
# build. Gates the four options below so a consumer doesn't get
# Chronicle's own tests/examples/bench/CLI force-built into their build --
# a real, previously-missing prerequisite for being a well-behaved
# dependency (docs/adr/0044-installable-package.md).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CHRONICLE_IS_TOP_LEVEL ON)
else()
    set(CHRONICLE_IS_TOP_LEVEL OFF)
endif()

option(CHRONICLE_BUILD_TESTS "Build chronicle-core unit tests" ${CHRONICLE_IS_TOP_LEVEL})
option(CHRONICLE_BUILD_EXAMPLES "Build chronicle examples" ${CHRONICLE_IS_TOP_LEVEL})
option(CHRONICLE_BUILD_BENCH "Build chronicle-bench microbenchmark suite" ${CHRONICLE_IS_TOP_LEVEL})
option(CHRONICLE_BUILD_TOOLS "Build chronicle-cli" ${CHRONICLE_IS_TOP_LEVEL})
option(CHRONICLE_BUILD_CODEGEN "Build chronicle-codegen (requires a Clang/LLVM CMake package install)" OFF)
option(CHRONICLE_BUILD_TRACY_EXAMPLE "Build the Tracy bridge example (requires a Tracy CMake package install)" OFF)
option(CHRONICLE_BUILD_ENTT_ADAPTER "Build chronicle-adapter-entt and its example (requires an EnTT CMake package install)" OFF)
option(CHRONICLE_BUILD_MEMCPY_SHIM "Build the Windows Detours-based memcpy interposition demo (requires Detours; Windows only, opt-in diagnostic tool, not part of chronicle-core)" OFF)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src)

if(CHRONICLE_IS_TOP_LEVEL)
    include(CMakePackageConfigHelpers)

    install(EXPORT ChronicleTargets
        FILE ChronicleTargets.cmake
        NAMESPACE chronicle::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/chronicle
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ChronicleConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/ChronicleConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/chronicle
    )
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/ChronicleConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/ChronicleConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/ChronicleConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/chronicle
    )
endif()

if(CHRONICLE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
endif()

if(CHRONICLE_BUILD_EXAMPLES)
    add_subdirectory(examples/minimal)
    add_subdirectory(examples/container)
    add_subdirectory(examples/map)
    add_subdirectory(examples/export)
    add_subdirectory(examples/allocator)
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

if(CHRONICLE_BUILD_MEMCPY_SHIM)
    if(WIN32)
        add_subdirectory(tools/memcpy-shim)
    else()
        message(STATUS "chronicle-memcpy-shim: Windows-only (Detours-based); skipping on this platform. "
                        "See docs/adr/0029-memcpy-interposition.md for the (untested-in-this-project) "
                        "LD_PRELOAD sketch for Linux.")
    endif()
endif()
