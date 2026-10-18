cmake_minimum_required(VERSION 3.24)
project(kompoz VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
target_include_directories(kompoz_core PUBLIC include)
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
        add_subdirectory(third_party/SDL)
        foreach(feature ZLIB BZIP2 PNG HARFBUZZ BROTLI)
            set(FT_DISABLE_${feature} ON CACHE BOOL "" FORCE)
        endforeach()
        add_subdirectory(third_party/freetype EXCLUDE_FROM_ALL)
        if(NOT TARGET Freetype::Freetype)
            add_library(Freetype::Freetype ALIAS freetype)
        endif()
        # HarfBuzz as one translation unit, with its FreeType font functions.
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/src/harfbuzz.cc)
            add_library(kompoz_harfbuzz STATIC third_party/harfbuzz/src/harfbuzz.cc)
            target_compile_definitions(kompoz_harfbuzz PRIVATE HAVE_FREETYPE=1)
            target_include_directories(kompoz_harfbuzz PUBLIC third_party/harfbuzz/src)
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
    # Images (AsyncImage): Android, Windows and the web decode and download with the platform's
    # own; elsewhere libjpeg-turbo, libwebp and libcurl, where they are installed.
    if(WIN32)
        target_link_libraries(kompoz_sdl3 PUBLIC windowscodecs ole32 wininet)
    elseif(NOT ANDROID AND NOT EMSCRIPTEN)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(KOMPOZ_TURBOJPEG QUIET IMPORTED_TARGET libturbojpeg)
            pkg_check_modules(KOMPOZ_WEBP QUIET IMPORTED_TARGET libwebp)
            pkg_check_modules(KOMPOZ_CURL QUIET IMPORTED_TARGET libcurl)
        endif()
        foreach(dep TURBOJPEG WEBP CURL)
            if(KOMPOZ_${dep}_FOUND)
                target_link_libraries(kompoz_sdl3 PUBLIC PkgConfig::KOMPOZ_${dep})
                target_compile_definitions(kompoz_sdl3 PRIVATE KOMPOZ_${dep}=1)
            endif()
        endforeach()
        if(NOT KOMPOZ_TURBOJPEG_FOUND OR NOT KOMPOZ_WEBP_FOUND OR NOT KOMPOZ_CURL_FOUND)
            message(STATUS "Kompoz: images: JPEG ${KOMPOZ_TURBOJPEG_FOUND}, WebP ${KOMPOZ_WEBP_FOUND}, HTTP ${KOMPOZ_CURL_FOUND} (libturbojpeg, libwebp, libcurl)")
        endif()
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
        target_sources(kompoz_sdl3 PRIVATE platform/sdl3/desktop_accent.cpp)
        target_compile_definitions(kompoz_sdl3 PRIVATE KOMPOZ_DESKTOP_ACCENT=1)
        target_link_libraries(kompoz_sdl3 PUBLIC ${CMAKE_DL_LIBS})
    endif()
    if(WIN32 OR (UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN))
        # The wallpaper's own colors, found and picked on a thread (Android's come from the system).
        target_sources(kompoz_sdl3 PRIVATE platform/sdl3/wallpaper_colors.cpp)
        target_compile_definitions(kompoz_sdl3 PRIVATE KOMPOZ_WALLPAPER_COLORS=1)
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
