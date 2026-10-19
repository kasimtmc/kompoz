# An overlay port that builds Kompoz from the repository it is in:
#   vcpkg install kompoz --overlay-ports=<kompoz>/packaging/vcpkg/ports
# AsyncImage's optional JPEG and WebP decoders and HTTP downloads on Linux and macOS (libturbojpeg,
# libwebp, libcurl, found with pkg-config) are left out here, so that nothing comes from outside
# vcpkg; Windows decodes and downloads with its own.
set(SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../../..")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DKOMPOZ_VENDORED_DEPS=OFF
        -DKOMPOZ_INSTALL=ON
        -DKOMPOZ_BUILD_TESTS=OFF
        -DKOMPOZ_BUILD_EXAMPLES=OFF
        -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/kompoz)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
