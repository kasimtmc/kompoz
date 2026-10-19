# An app built against an installed Kompoz (cmake --install), as the README shows: CI checks the
# package with it. -DCMAKE_PREFIX_PATH=<where Kompoz was installed>.
cmake_minimum_required(VERSION 3.24)
project(kompoz_package_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(kompoz 0.1 REQUIRED)
add_executable(demo main.cpp)
target_link_libraries(demo PRIVATE kompoz::sdl3)
kompoz_copy_assets(demo)
