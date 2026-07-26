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

# provenance.hpp uses std::stacktrace (docs/adr/0032). On libstdc++, its
# implementation lives in a separate static library whose name changed
# across GCC versions -- stdc++_libbacktrace (GCC <= 13) vs. stdc++exp
# (GCC 14+) -- and linking fails outright without it (real CI failure:
# "undefined reference to std::stacktrace_entry::..."/"__glibcxx_backtrace_*",
# caught only by reading the actual ubuntu-gcc/ubuntu-clang CI logs, not
# from any local Windows build). Detected via a real compile+link check
# rather than branching on compiler ID/version, since the two Ubuntu CI
# jobs in this project turned out to pick up *different* libstdc++
# versions (13 vs. 14) despite both running on "ubuntu-latest" --
# libc++/MSVC STL need neither and fall through to the no-op case.
include(CheckCXXSourceCompiles)
include(CMakePushCheckState)

set(CHRONICLE_STACKTRACE_TEST_SOURCE [[
#include <stacktrace>
int main() { auto s = std::stacktrace::current(); return static_cast<int>(s.size()); }
]])

cmake_push_check_state()
set(CMAKE_REQUIRED_FLAGS "-std=c++23")
check_cxx_source_compiles("${CHRONICLE_STACKTRACE_TEST_SOURCE}" CHRONICLE_STACKTRACE_LINKS_UNAIDED)
if(NOT CHRONICLE_STACKTRACE_LINKS_UNAIDED)
    set(CMAKE_REQUIRED_LIBRARIES stdc++exp)
    check_cxx_source_compiles("${CHRONICLE_STACKTRACE_TEST_SOURCE}" CHRONICLE_STACKTRACE_NEEDS_STDCXX_EXP)
    if(NOT CHRONICLE_STACKTRACE_NEEDS_STDCXX_EXP)
        set(CMAKE_REQUIRED_LIBRARIES stdc++_libbacktrace)
        check_cxx_source_compiles("${CHRONICLE_STACKTRACE_TEST_SOURCE}" CHRONICLE_STACKTRACE_NEEDS_STDCXX_LIBBACKTRACE)
    endif()
endif()
cmake_pop_check_state()

if(CHRONICLE_STACKTRACE_NEEDS_STDCXX_EXP)
    target_link_libraries(chronicle-core INTERFACE stdc++exp)
elseif(CHRONICLE_STACKTRACE_NEEDS_STDCXX_LIBBACKTRACE)
    target_link_libraries(chronicle-core INTERFACE stdc++_libbacktrace)
endif()

if(CHRONICLE_IS_TOP_LEVEL)
    include(GNUInstallDirs)

    install(TARGETS chronicle-core
        EXPORT ChronicleTargets
    )
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()
