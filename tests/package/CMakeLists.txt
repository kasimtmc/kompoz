# An app built against Kompoz as the README shows: CI checks the package with it, and Kompoz used as a
# subdirectory. -DCMAKE_PREFIX_PATH=<where Kompoz was installed>, or -DKOMPOZ_SOURCE_DIR=<Kompoz's tree>
# (add_subdirectory, from outside that tree).
cmake_minimum_required(VERSION 3.24)
project(kompoz_package_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

if(KOMPOZ_SOURCE_DIR)
    set(KOMPOZ_BUILD_EXAMPLES OFF)
    set(KOMPOZ_BUILD_TESTS OFF)
    add_subdirectory(${KOMPOZ_SOURCE_DIR} kompoz)
else()
    find_package(kompoz 0.1 REQUIRED)
endif()
add_executable(demo main.cpp greeting.cpp)
target_link_libraries(demo PRIVATE kompoz::app)
kompozable(demo)  # greeting.cpp's @Kompozable function, in Kotlin's syntax
kompoz_copy_assets(demo)
