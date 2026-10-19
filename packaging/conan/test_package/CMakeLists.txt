cmake_minimum_required(VERSION 3.24)
project(kompoz_conan_test LANGUAGES C CXX)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/package package)
