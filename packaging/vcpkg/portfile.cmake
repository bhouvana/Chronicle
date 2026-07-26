vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO bhouvana/Chronicle
    REF "v${VERSION}"
    SHA512 b115590137178c945181c25f370c6e2d84443d2929a462820643b8f45d83203e5208cbb6ac70bdc522e8c57b0165872e69e6c907318995dd47472dd1a4324806
    HEAD_REF main
)

set(VCPKG_BUILD_TYPE release) # header-only port

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DCHRONICLE_BUILD_TESTS=OFF
        -DCHRONICLE_BUILD_EXAMPLES=OFF
        -DCHRONICLE_BUILD_BENCH=OFF
        -DCHRONICLE_BUILD_TOOLS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/chronicle)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
