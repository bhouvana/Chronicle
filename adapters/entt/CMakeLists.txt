# chronicle-adapter-entt: opt-in, like tools/codegen and the Tracy bridge --
# most consumers of chronicle-core won't have EnTT installed, and this
# adapter's whole reason for living in adapters/ rather than
# include/chronicle/ is that it has a real external dependency
# (docs/11-repository-structure-and-standards.md, docs/adr/0015-entt-adapter.md).
find_package(EnTT CONFIG QUIET)

if(NOT EnTT_FOUND)
    message(STATUS "chronicle-adapter-entt: EnTT CMake package not found, skipping (set CMAKE_PREFIX_PATH to an EnTT install to enable)")
    return()
endif()

add_library(chronicle-adapter-entt INTERFACE)
target_include_directories(chronicle-adapter-entt INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(chronicle-adapter-entt INTERFACE chronicle::core EnTT::EnTT)
target_compile_features(chronicle-adapter-entt INTERFACE cxx_std_23)
