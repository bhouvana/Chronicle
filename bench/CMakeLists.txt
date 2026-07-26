add_executable(chronicle-bench main.cpp)
target_link_libraries(chronicle-bench PRIVATE chronicle::core)

# Release-like flags even under a Debug top-level build, since an
# unoptimized build's numbers are meaningless for anything in
# docs/09-performance.md -- a debug-mode ns/op figure would misrepresent
# the mutex-staging-deque cost this suite exists to measure honestly.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(chronicle-bench PRIVATE -O2 -g)
else()
    target_compile_options(chronicle-bench PRIVATE /O2 /Zi)
    target_link_options(chronicle-bench PRIVATE /DEBUG)
endif()
