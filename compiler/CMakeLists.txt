# kompozc, the compiler for @Composable functions written in Compose's own syntax in C++ sources
# (kompoz_compose, cmake/KompozCompose.cmake). Built with Kompoz on the machine that builds; built
# on its own from here (for that machine) where Kompoz is built for another: Android, the web.
cmake_minimum_required(VERSION 3.24)
project(kompozc LANGUAGES CXX)
if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

add_executable(kompozc main.cpp translate.cpp lsp.cpp lexer.cpp parser.cpp codegen.cpp headers.cpp)
find_package(Threads REQUIRED)
target_link_libraries(kompozc PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(kompozc PRIVATE /W4 /utf-8)
else()
    target_compile_options(kompozc PRIVATE -Wall -Wextra)
endif()
