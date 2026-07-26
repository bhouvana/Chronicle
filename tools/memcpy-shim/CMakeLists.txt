# docs/12-future-research-topics.md topic 3, docs/adr/0029-memcpy-interposition.md.
# Windows-only, Detours-based; detours has no CMake package config (verified
# directly -- no *-config.cmake under its vcpkg share/ directory, unlike
# zstd/EnTT/httplib), so this uses find_path/find_library per detours' own
# vcpkg usage note rather than find_package(... CONFIG).
find_path(DETOURS_INCLUDE_DIR "detours/detours.h")
find_library(DETOURS_LIBRARY detours)

if(DETOURS_INCLUDE_DIR AND DETOURS_LIBRARY)
    add_library(chronicle-memcpy-shim STATIC shim.cpp)
    target_include_directories(chronicle-memcpy-shim PUBLIC ${DETOURS_INCLUDE_DIR})
    target_link_libraries(chronicle-memcpy-shim PUBLIC chronicle::core ${DETOURS_LIBRARY})

    add_executable(chronicle-memcpy-shim-demo demo.cpp)
    target_link_libraries(chronicle-memcpy-shim-demo PRIVATE chronicle-memcpy-shim)
else()
    message(STATUS "chronicle-memcpy-shim: Detours not found (looked for detours/detours.h and "
                    "the detours library) -- skipping. Install via vcpkg (vcpkg install detours) "
                    "and configure with -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake.")
endif()
