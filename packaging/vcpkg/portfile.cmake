vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO bhouvana/Chronicle
    REF "v${VERSION}"
    SHA512 84ea8f26d90d2766b777ed252d8a44301cf3439056813a4dcc834659d63b3618184e9919f24839da38ead290ce644ab307b7a4d7c2c1f2619e547ca5eda18efe
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
