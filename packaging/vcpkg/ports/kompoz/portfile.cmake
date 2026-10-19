# An overlay port that builds Kompoz from the repository it is in:
#   vcpkg install kompoz --overlay-ports=<kompoz>/packaging/vcpkg/ports
# Its features (on by default) are AsyncImage's libraries on Linux and macOS: curl (HTTP),
# jpeg (libjpeg-turbo), webp (libwebp); Windows decodes and downloads with its own.
set(SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../../..")

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        curl KOMPOZ_WITH_CURL
        jpeg KOMPOZ_WITH_JPEG
        webp KOMPOZ_WITH_WEBP
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DKOMPOZ_VENDORED_DEPS=OFF
        -DKOMPOZ_INSTALL=ON
        -DKOMPOZ_BUILD_TESTS=OFF
        -DKOMPOZ_BUILD_EXAMPLES=OFF
        # vcpkg's own CMake packages only, never a system library through pkg-config.
        -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/kompoz)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
