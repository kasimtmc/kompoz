# kompozc, the compiler for @Kompozable functions written in Kotlin's syntax in C++ sources
# (kompozable, cmake/Kompozable.cmake). Built with Kompoz on the machine that builds; built
# on its own from here (for that machine) where Kompoz is built for another: Android, the web.
cmake_minimum_required(VERSION 3.24)
project(kompozc LANGUAGES CXX)
if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

add_executable(kompozc main.cpp translate.cpp lsp.cpp kate.cpp lexer.cpp parser.cpp codegen.cpp headers.cpp)

# Kate's highlighting definition for Kompoz C++ (kate/kompoz-cpp.xml), built in for kompozc --kate.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/kate/kompoz-cpp.xml)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/kate/kompoz-cpp.xml kompoz_kate_syntax)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generated/kate_syntax.hpp.new
    "#pragma once\n// kate/kompoz-cpp.xml (compiler/CMakeLists.txt writes this)\nnamespace kompozc {\n"
    "inline constexpr char kateSyntax[] = R\"kompoz(${kompoz_kate_syntax})kompoz\";\n}\n")
file(COPY_FILE ${CMAKE_CURRENT_BINARY_DIR}/generated/kate_syntax.hpp.new ${CMAKE_CURRENT_BINARY_DIR}/generated/kate_syntax.hpp
    ONLY_IF_DIFFERENT)
target_include_directories(kompozc PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
find_package(Threads REQUIRED)
target_link_libraries(kompozc PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(kompozc PRIVATE /W4 /utf-8)
else()
    target_compile_options(kompozc PRIVATE -Wall -Wextra)
endif()
