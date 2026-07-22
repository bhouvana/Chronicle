# chronicle-codegen: opt-in only. Unlike chronicle-cli, this tool links
# against Clang/LLVM's own libraries, which most consumers of the Chronicle
# library will not have installed (the official prebuilt LLVM releases for
# Windows don't include the C++ dev libs at all -- see
# docs/adr/0012-chronicle-codegen-libtooling.md for how this was built and
# verified in an environment without a proper package-config install).
#
# find_package(Clang CONFIG) is the portable path: it works for any LLVM/
# Clang install that ships ClangConfig.cmake (vcpkg's llvm port, the LLVM
# project's own CMake install target, Linux distro dev packages). A from-
# source build/tree with LLVM_BUILD_TOOLS=OFF (as used for this tool's own
# development -- see the ADR) doesn't generate that config, so this target
# simply won't configure there; build it directly instead, per the ADR.
find_package(Clang CONFIG QUIET)

if(NOT Clang_FOUND)
    message(STATUS "chronicle-codegen: Clang CMake package not found, skipping (set CMAKE_PREFIX_PATH to an LLVM/Clang install to enable)")
    return()
endif()

add_executable(chronicle-codegen main.cpp)
target_compile_features(chronicle-codegen PRIVATE cxx_std_23)
target_include_directories(chronicle-codegen PRIVATE ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})
target_compile_definitions(chronicle-codegen PRIVATE ${LLVM_DEFINITIONS})
if(NOT LLVM_ENABLE_RTTI)
    if(MSVC)
        target_compile_options(chronicle-codegen PRIVATE /GR-)
    else()
        target_compile_options(chronicle-codegen PRIVATE -fno-rtti)
    endif()
endif()

target_link_libraries(chronicle-codegen PRIVATE
    clangTooling
    clangASTMatchers
    clangFrontend
    clangSerialization
    clangDriver
    clangParse
    clangSema
    clangAnalysis
    clangEdit
    clangAST
    clangLex
    clangBasic
)
