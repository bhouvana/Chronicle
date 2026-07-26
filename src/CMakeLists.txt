# chronicle-core is header-only for v0.1 (see docs/rfc/0001): Session and
# RetentionPolicy have no out-of-line logic yet that needs a translation
# unit. This target exists now so downstream targets (tests, examples,
# future adapters) have one stable name to link against; the day a
# non-template implementation file is needed (e.g. the lock-free ring
# buffer's platform-specific bits), it moves from INTERFACE to a regular
# library here without changing any consumer's CMake code.
add_library(chronicle-core INTERFACE)
add_library(chronicle::core ALIAS chronicle-core)

# The in-tree alias above (chronicle::core) is NOT what install(EXPORT ...)
# emits by default -- it would export as chronicle::chronicle-core (namespace
# + the real target name), silently breaking the chronicle::core spelling
# every doc/example promises for installed/find_package consumers. EXPORT_NAME
# makes the exported name match the alias without renaming the real target
# (which every other CMakeLists.txt in this tree still refers to as
# chronicle-core). Caught by real find_package() consumer-project
# verification, not assumed correct from reading the CMake.
set_target_properties(chronicle-core PROPERTIES EXPORT_NAME core)

target_include_directories(chronicle-core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(chronicle-core INTERFACE cxx_std_23)

find_package(Threads REQUIRED)
target_link_libraries(chronicle-core INTERFACE Threads::Threads)

if(CHRONICLE_IS_TOP_LEVEL)
    include(GNUInstallDirs)

    install(TARGETS chronicle-core
        EXPORT ChronicleTargets
    )
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()
