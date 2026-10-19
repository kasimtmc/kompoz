cmake_minimum_required(VERSION 3.24)
project(kompoz VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(GNUInstallDirs)

# Mobile platforms, the web and Windows have no system SDL3/FreeType packages, so build them
# from source.
if(ANDROID OR IOS OR EMSCRIPTEN)
    set(kompoz_mobile ON)
else()
    set(kompoz_mobile OFF)
endif()
if(kompoz_mobile OR WIN32)
    set(kompoz_vendored ON)
else()
    set(kompoz_vendored OFF)
endif()
# Linking the showcase (sanitizers, debug info) takes GNU ld about 25 s and lld about 1: on a
# desktop that has lld, it is used unless a linker was chosen (CMAKE_LINKER_TYPE, CMake 3.29+).
if(PROJECT_IS_TOP_LEVEL AND NOT DEFINED CMAKE_LINKER_TYPE AND NOT kompoz_mobile AND NOT WIN32 AND NOT APPLE
   AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
    include(CheckLinkerFlag)
    check_linker_flag(CXX "-fuse-ld=lld" kompoz_has_lld)
    if(kompoz_has_lld)
        set(CMAKE_LINKER_TYPE LLD)
    endif()
endif()
option(KOMPOZ_BUILD_SDL3 "Build the SDL3 platform backend" ON)
# cmake --install: the headers, the libraries, the assets and a package for find_package(kompoz),
# with SDL3, FreeType and HarfBuzz when they are built here (see the end).
if(PROJECT_IS_TOP_LEVEL AND NOT kompoz_mobile)
    set(kompoz_install_default ON)
else()
    set(kompoz_install_default OFF)
endif()
option(KOMPOZ_INSTALL "Install rules and a CMake package for find_package(kompoz)" ${kompoz_install_default})
option(KOMPOZ_BUILD_EXAMPLES "Build the example apps (needs the SDL3 backend)" ON)
option(KOMPOZ_VENDORED_DEPS "Build SDL3, FreeType and HarfBuzz from third_party/ (run scripts/fetch-deps.sh)" ${kompoz_vendored})
if(kompoz_mobile)
    option(KOMPOZ_BUILD_TESTS "Build the headless tests" OFF)
else()
    option(KOMPOZ_BUILD_TESTS "Build the headless tests" ON)
endif()

if(EMSCRIPTEN)
    # No threads on the web (they would need cross-origin isolated pages): mutexes are no-ops.
    add_library(Threads::Threads INTERFACE IMPORTED)
else()
    find_package(Threads REQUIRED)
endif()

# Platform-independent core: runtime, layout, input, widgets. No system dependencies.
add_library(kompoz_core STATIC
    src/runtime.cpp
    src/saved_value.cpp
    src/animated.cpp
    src/bidi.cpp
    src/adaptive.cpp
    src/animation.cpp
    src/bottom_sheet.cpp
    src/carousel.cpp
    src/components.cpp
    src/coroutines.cpp
    src/date_picker.cpp
    src/disk_cache.cpp
    src/display_list.cpp
    src/strings.cpp
    src/time_picker.cpp
    src/tooltip.cpp
    src/exposed_dropdown.cpp
    src/flexbox.cpp
    src/grid.cpp
    src/hct.cpp
    src/floating_toolbar.cpp
    src/draw.cpp
    src/dynamic_color.cpp
    src/graphics.cpp
    src/text.cpp
    src/html.cpp
    src/text_measure.cpp
    src/gestures.cpp
    src/layout.cpp
    src/lifecycle.cpp
    src/loading_indicator.cpp
    src/material_shapes.cpp
    src/marquee.cpp
    src/menu_bar.cpp
    src/scrollbar.cpp
    src/host.cpp
    src/interaction.cpp
    src/lazy.cpp
    src/material.cpp
    src/navigation.cpp
    src/nav_host.cpp
    src/nested_scroll.cpp
    src/pager.cpp
    src/platform.cpp
    src/preview.cpp
    src/pull_refresh.cpp
    src/search.cpp
    src/shared_transition.cpp
    src/short_navigation_bar.cpp
    src/overlays.cpp
    src/vector.cpp
    src/viewmodel.cpp
    src/wavy_progress.cpp
    src/widgets.cpp)
target_include_directories(kompoz_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                              $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(kompoz_core PROPERTIES EXPORT_NAME core)
add_library(kompoz::core ALIAS kompoz_core)
target_link_libraries(kompoz_core PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(kompoz_core PRIVATE /W4)
    # KOMPOZ_ROUTE needs the conforming preprocessor (__VA_OPT__), in apps too.
    target_compile_options(kompoz_core PUBLIC /Zc:preprocessor)
else()
    # Designated initializers with omitted fields are how named arguments are written here.
    target_compile_options(kompoz_core PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

if(KOMPOZ_BUILD_TESTS)
    enable_testing()
    add_executable(kompoz_tests tests/kompoz_tests.cpp)
    target_link_libraries(kompoz_tests PRIVATE kompoz_core)
    add_test(NAME kompoz_tests COMMAND kompoz_tests)
endif()

if(KOMPOZ_BUILD_SDL3)
    if(KOMPOZ_VENDORED_DEPS)
        foreach(dep SDL freetype)
            if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/${dep}/CMakeLists.txt)
                message(FATAL_ERROR "third_party/${dep} is missing. Run scripts/fetch-deps.sh first.")
            endif()
        endforeach()
        if(EMSCRIPTEN OR WIN32)  # one program to ship, without SDL3.dll beside it
            set(SDL_SHARED OFF CACHE BOOL "" FORCE)
            set(SDL_STATIC ON CACHE BOOL "" FORCE)
        else()
            set(SDL_SHARED ON CACHE BOOL "" FORCE)
            set(SDL_STATIC OFF CACHE BOOL "" FORCE)
        endif()
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        # Installed with Kompoz when it is (its own package, which Kompoz's finds).
        set(SDL_INSTALL ${KOMPOZ_INSTALL} CACHE BOOL "" FORCE)
        set(SDL_INSTALL_CPACK OFF CACHE BOOL "" FORCE)
        set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)
        add_subdirectory(third_party/SDL)
        foreach(feature ZLIB BZIP2 PNG HARFBUZZ BROTLI)
            set(FT_DISABLE_${feature} ON CACHE BOOL "" FORCE)
        endforeach()
        if(KOMPOZ_INSTALL)  # installed with Kompoz, with its own package
            set(SKIP_INSTALL_ALL OFF)
            add_subdirectory(third_party/freetype)
        else()
            add_subdirectory(third_party/freetype EXCLUDE_FROM_ALL)
        endif()
        if(NOT TARGET Freetype::Freetype)
            add_library(Freetype::Freetype ALIAS freetype)
        endif()
        # HarfBuzz as one translation unit, with its FreeType font functions.
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/src/harfbuzz.cc)
            add_library(kompoz_harfbuzz STATIC third_party/harfbuzz/src/harfbuzz.cc)
            target_compile_definitions(kompoz_harfbuzz PRIVATE HAVE_FREETYPE=1)
            target_include_directories(kompoz_harfbuzz PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/src>)
            set_target_properties(kompoz_harfbuzz PROPERTIES EXPORT_NAME harfbuzz)  # (in Kompoz's package)
            target_link_libraries(kompoz_harfbuzz PUBLIC Freetype::Freetype)
            add_library(harfbuzz::harfbuzz ALIAS kompoz_harfbuzz)
        endif()
    else()
        find_package(SDL3 REQUIRED CONFIG)
        find_package(Freetype REQUIRED)
        find_package(harfbuzz CONFIG QUIET)
    endif()

    # An OBJECT library because the backend defines the entry point (main, or SDL_main on
    # Android/iOS), which must always end up in the final binary.
    add_library(kompoz_sdl3 OBJECT platform/sdl3/sdl3_app.cpp platform/sdl3/image_loader.cpp)
    target_link_libraries(kompoz_sdl3 PUBLIC kompoz_core SDL3::SDL3 Freetype::Freetype)
    set_target_properties(kompoz_sdl3 PROPERTIES EXPORT_NAME sdl3)
    add_library(kompoz::sdl3 ALIAS kompoz_sdl3)
    # Images (AsyncImage): Android, Windows and the web decode and download with the platform's
    # own; Linux and macOS with libjpeg-turbo, libwebp and libcurl (cmake/KompozImageLibraries.cmake).
    if(WIN32)
        target_link_libraries(kompoz_sdl3 PUBLIC windowscodecs ole32 wininet)
    elseif(NOT ANDROID AND NOT EMSCRIPTEN)
        include(cmake/KompozImageLibraries.cmake)
        set(kompoz_image_libraries)
        foreach(lib JPEG WEBP CURL)
            set(KOMPOZ_WITH_${lib} AUTO CACHE STRING "AsyncImage's ${lib} library (Linux, macOS): AUTO (when found), ON (required) or OFF")
            set_property(CACHE KOMPOZ_WITH_${lib} PROPERTY STRINGS AUTO ON OFF)
            kompoz_find_image_library(${lib} ${KOMPOZ_WITH_${lib}})
            if(KOMPOZ_${lib}_TARGET)
                target_link_libraries(kompoz_sdl3 PUBLIC ${KOMPOZ_${lib}_TARGET})
                list(APPEND kompoz_image_libraries ${lib})
            endif()
        endforeach()
        if(KOMPOZ_JPEG_TARGET)
            target_compile_definitions(kompoz_sdl3 PRIVATE KOMPOZ_TURBOJPEG=1)
            if(KOMPOZ_JPEG_VERSION VERSION_GREATER_EQUAL 3.0)
                target_compile_definitions(kompoz_sdl3 PRIVATE KOMPOZ_TURBOJPEG3=1)
            endif()
        endif()
        if(KOMPOZ_WEBP_TARGET)
            target_compile_definitions(kompoz_sdl3 PRIVATE KOMPOZ_WEBP=1)
        endif()
        if(KOMPOZ_CURL_TARGET)
            target_compile_definitions(kompoz_sdl3 PRIVATE KOMPOZ_CURL=1)
        endif()
        message(STATUS "Kompoz: images: JPEG ${KOMPOZ_JPEG_HOW} ${KOMPOZ_JPEG_VERSION}, WebP ${KOMPOZ_WEBP_HOW}, HTTP ${KOMPOZ_CURL_HOW} (libjpeg-turbo, libwebp, libcurl; empty: not used)")
    endif()
    # Text is shaped with HarfBuzz when it is there: ligatures, kerning, Arabic joining, Indic
    # scripts, emoji sequences.
    if(TARGET harfbuzz::harfbuzz)
        target_link_libraries(kompoz_sdl3 PUBLIC harfbuzz::harfbuzz)
        target_compile_definitions(kompoz_sdl3 PRIVATE KOMPOZ_HARFBUZZ=1)
    else()
        message(STATUS "Kompoz: HarfBuzz not found; text is drawn character by character (no ligatures, joining or emoji sequences)")
    endif()
    if(ANDROID)
        target_sources(kompoz_sdl3 PRIVATE platform/sdl3/android_ime.cpp)  # + platform/android/java
    elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
        # The desktop's accent color from the XDG portal (libdbus loaded at run time): dynamic color.
        # Screen readers read the semantics tree through AT-SPI (the same libdbus).
        target_sources(kompoz_sdl3 PRIVATE platform/sdl3/desktop_accent.cpp platform/sdl3/atspi_bridge.cpp)
        target_compile_definitions(kompoz_sdl3 PRIVATE KOMPOZ_DESKTOP_ACCENT=1 KOMPOZ_ATSPI=1)
        target_link_libraries(kompoz_sdl3 PUBLIC ${CMAKE_DL_LIBS})
    elseif(WIN32)
        # Windows' accent color (DWM's, in the registry): dynamic color. Screen readers read the
        # semantics tree through UI Automation.
        target_sources(kompoz_sdl3 PRIVATE platform/sdl3/desktop_accent_windows.cpp platform/sdl3/uia_bridge.cpp)
        target_compile_definitions(kompoz_sdl3 PRIVATE KOMPOZ_DESKTOP_ACCENT=1 KOMPOZ_UIA=1)
        target_link_libraries(kompoz_sdl3 PUBLIC uiautomationcore oleaut32)
    endif()
    if(WIN32 OR (UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN))
        # The wallpaper's own colors, found and picked on a thread (Android's come from the system).
        target_sources(kompoz_sdl3 PRIVATE platform/sdl3/wallpaper_colors.cpp)
        target_compile_definitions(kompoz_sdl3 PRIVATE KOMPOZ_WALLPAPER_COLORS=1)
    endif()
    # The platform code that runs without a window (the wallpaper's colors, AT-SPI; UI Automation on
    # Windows), tested on its own.
    if(KOMPOZ_BUILD_TESTS AND UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
        add_executable(kompoz_platform_tests tests/platform_tests.cpp platform/sdl3/wallpaper_colors.cpp
                       platform/sdl3/image_loader.cpp platform/sdl3/atspi_bridge.cpp)
        target_include_directories(kompoz_platform_tests PRIVATE platform/sdl3)
        target_link_libraries(kompoz_platform_tests PRIVATE $<TARGET_PROPERTY:kompoz_sdl3,INTERFACE_LINK_LIBRARIES>)
        target_compile_definitions(kompoz_platform_tests PRIVATE $<TARGET_PROPERTY:kompoz_sdl3,COMPILE_DEFINITIONS>)
        add_test(NAME kompoz_platform_tests COMMAND kompoz_platform_tests)
    elseif(KOMPOZ_BUILD_TESTS AND WIN32)
        add_executable(kompoz_platform_tests tests/platform_tests_windows.cpp platform/sdl3/uia_bridge.cpp)
        target_include_directories(kompoz_platform_tests PRIVATE platform/sdl3)
        target_link_libraries(kompoz_platform_tests PRIVATE $<TARGET_PROPERTY:kompoz_sdl3,INTERFACE_LINK_LIBRARIES>)
        add_test(NAME kompoz_platform_tests COMMAND kompoz_platform_tests)
    endif()

    # For an app's CMakeLists: copies what every app ships (Kompoz's assets/: the bundled fonts and
    # the licenses) next to `target`'s binary each time it is built (desktops; Android and the web
    # take assets/ into the APK and the page's data, as the showcase does).
    function(kompoz_copy_assets target)
        add_custom_command(TARGET ${target} POST_BUILD
                           COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/assets $<TARGET_FILE_DIR:${target}>)
    endfunction()

    if(KOMPOZ_BUILD_EXAMPLES)
        add_subdirectory(examples/showcase)
    endif()
endif()

# cmake --install: Kompoz's headers, kompoz_core, the SDL3 backend's objects (the entry point) and the
# assets, and a CMake package: find_package(kompoz) gives kompoz::core, kompoz::sdl3 and
# kompoz_copy_assets. SDL3 and FreeType built here are installed beside it with their own packages,
# HarfBuzz built here in Kompoz's; the system's are found again where they were.
if(KOMPOZ_INSTALL)
    include(CMakePackageConfigHelpers)
    set(kompoz_cmake_dir ${CMAKE_INSTALL_LIBDIR}/cmake/kompoz)
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(TARGETS kompoz_core EXPORT kompozTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    set(KOMPOZ_PACKAGE_SDL3 OFF)
    if(TARGET kompoz_sdl3)
        set(KOMPOZ_PACKAGE_SDL3 ON)
        install(TARGETS kompoz_sdl3 EXPORT kompozTargets OBJECTS DESTINATION ${CMAKE_INSTALL_LIBDIR}/kompoz)
        if(TARGET kompoz_harfbuzz)
            install(TARGETS kompoz_harfbuzz EXPORT kompozTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        endif()
        install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/kompoz/assets)
    endif()
    install(EXPORT kompozTargets NAMESPACE kompoz:: DESTINATION ${kompoz_cmake_dir})
    # What the package finds again: kompoz_sdl3's dependencies, the way they were found here.
    set(KOMPOZ_PACKAGE_VENDORED ${KOMPOZ_VENDORED_DEPS})
    if(KOMPOZ_VENDORED_DEPS)
        set(KOMPOZ_PACKAGE_HARFBUZZ OFF)  # (built here: in Kompoz's own package)
    else()
        set(KOMPOZ_PACKAGE_HARFBUZZ ${harfbuzz_FOUND})
    endif()
    foreach(lib JPEG WEBP CURL)
        set(KOMPOZ_PACKAGE_${lib}_HOW "${KOMPOZ_${lib}_HOW}")
    endforeach()
    install(FILES cmake/KompozImageLibraries.cmake DESTINATION ${kompoz_cmake_dir})
    configure_package_config_file(cmake/kompozConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/kompozConfig.cmake
                                  INSTALL_DESTINATION ${kompoz_cmake_dir} PATH_VARS CMAKE_INSTALL_DATADIR)
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/kompozConfigVersion.cmake COMPATIBILITY SameMinorVersion)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kompozConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/kompozConfigVersion.cmake
            DESTINATION ${kompoz_cmake_dir})
endif()
