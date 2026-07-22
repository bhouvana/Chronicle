# chronicle-core is header-only for v0.1 (see docs/rfc/0001): Session and
# RetentionPolicy have no out-of-line logic yet that needs a translation
# unit. This target exists now so downstream targets (tests, examples,
# future adapters) have one stable name to link against; the day a
# non-template implementation file is needed (e.g. the lock-free ring
# buffer's platform-specific bits), it moves from INTERFACE to a regular
# library here without changing any consumer's CMake code.
add_library(chronicle-core INTERFACE)
add_library(chronicle::core ALIAS chronicle-core)

target_include_directories(chronicle-core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(chronicle-core INTERFACE cxx_std_23)

find_package(Threads REQUIRED)
target_link_libraries(chronicle-core INTERFACE Threads::Threads)
