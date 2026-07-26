add_executable(chronicle-core-tests
    main.cpp
    tracked_test.cpp
    tracked_vector_test.cpp
    tracked_map_test.cpp
    io_test.cpp
    concurrency_test.cpp
    call_site_test.cpp
    tracked_type_test.cpp
    hlc_test.cpp
    tracked_memory_resource_test.cpp
    race_test.cpp
    anomaly_test.cpp
    embedded_test.cpp
    interposition_registry_test.cpp
)
target_link_libraries(chronicle-core-tests PRIVATE chronicle::core)
target_include_directories(chronicle-core-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Zstd compression tests (docs/adr/0014-storage-engine-compression.md) are
# opt-in, same as tools/codegen and the Tracy example: most environments
# building this default-on test target won't have Zstd's CMake package
# installed, so the file is only added -- and only then does this target
# gain a Zstd dependency -- when find_package succeeds AND the specific
# imported target this project expects actually exists. zstd_FOUND alone
# isn't sufficient: a real CI run (ubuntu-latest) has a *system* zstd CMake
# package that satisfies `find_package(zstd CONFIG)` but does not define
# `zstd::libzstd` (that's vcpkg's port-specific target name), which failed
# the whole configure with "target was not found" before this check was
# added -- see docs/adr/0017-ci-performance-gate.md.
find_package(zstd CONFIG QUIET)
if(zstd_FOUND AND TARGET zstd::libzstd)
    target_sources(chronicle-core-tests PRIVATE compression_test.cpp)
    target_link_libraries(chronicle-core-tests PRIVATE zstd::libzstd)
else()
    message(STATUS "chronicle-core-tests: no compatible Zstd CMake package found, skipping compression_test.cpp")
endif()

# chronicle-adapter-entt tests (docs/adr/0015-entt-adapter.md) -- same
# opt-in reasoning, and same TARGET guard, as the Zstd tests above.
find_package(EnTT CONFIG QUIET)
if(EnTT_FOUND AND TARGET EnTT::EnTT)
    target_sources(chronicle-core-tests PRIVATE entt_adapter_test.cpp)
    target_include_directories(chronicle-core-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../adapters/entt/include)
    target_link_libraries(chronicle-core-tests PRIVATE EnTT::EnTT)
else()
    message(STATUS "chronicle-core-tests: no compatible EnTT CMake package found, skipping entt_adapter_test.cpp")
endif()

add_test(NAME chronicle-core-tests COMMAND chronicle-core-tests)
