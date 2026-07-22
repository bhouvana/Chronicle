add_executable(chronicle-core-tests
    main.cpp
    tracked_test.cpp
    tracked_vector_test.cpp
    tracked_map_test.cpp
    io_test.cpp
    concurrency_test.cpp
    call_site_test.cpp
    tracked_type_test.cpp
)
target_link_libraries(chronicle-core-tests PRIVATE chronicle::core)
target_include_directories(chronicle-core-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Zstd compression tests (docs/adr/0014-storage-engine-compression.md) are
# opt-in, same as tools/codegen and the Tracy example: most environments
# building this default-on test target won't have Zstd's CMake package
# installed, so the file is only added -- and only then does this target
# gain a Zstd dependency -- when find_package succeeds.
find_package(zstd CONFIG QUIET)
if(zstd_FOUND)
    target_sources(chronicle-core-tests PRIVATE compression_test.cpp)
    target_link_libraries(chronicle-core-tests PRIVATE zstd::libzstd)
else()
    message(STATUS "chronicle-core-tests: Zstd CMake package not found, skipping compression_test.cpp")
endif()

# chronicle-adapter-entt tests (docs/adr/0015-entt-adapter.md) -- same
# opt-in reasoning as the Zstd tests above.
find_package(EnTT CONFIG QUIET)
if(EnTT_FOUND)
    target_sources(chronicle-core-tests PRIVATE entt_adapter_test.cpp)
    target_include_directories(chronicle-core-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../adapters/entt/include)
    target_link_libraries(chronicle-core-tests PRIVATE EnTT::EnTT)
else()
    message(STATUS "chronicle-core-tests: EnTT CMake package not found, skipping entt_adapter_test.cpp")
endif()

add_test(NAME chronicle-core-tests COMMAND chronicle-core-tests)
