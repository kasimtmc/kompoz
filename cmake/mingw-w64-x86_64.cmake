# Cross-compiling for Windows (x86-64) with MinGW-w64 from Linux:
#   cmake -S . -B build-windows -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
# SDL3, FreeType and HarfBuzz are built from third_party/ (scripts/fetch-deps.sh).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
# The MinGW sysroot, and whatever roots were given (-DCMAKE_FIND_ROOT_PATH=<an install of Kompoz, ...>).
if(NOT "/usr/x86_64-w64-mingw32" IN_LIST CMAKE_FIND_ROOT_PATH)
    list(APPEND CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
