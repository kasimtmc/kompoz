cmake_minimum_required(VERSION 3.24)
project(kompoz VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(GNUInstallDirs)

# Mobile platforms, the web and Windows have no system FreeType or libpng packages, so build them
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
option(KOMPOZ_BUILD_APP "Build the platform layer apps run on (kompoz::app)" ON)
# cmake --install: the headers, the libraries, the assets and a package for find_package(kompoz),
# with FreeType, HarfBuzz, zlib and libpng when they are built here (see the end).
if(PROJECT_IS_TOP_LEVEL AND NOT kompoz_mobile)
    set(kompoz_install_default ON)
else()
    set(kompoz_install_default OFF)
endif()
option(KOMPOZ_INSTALL "Install rules and a CMake package for find_package(kompoz)" ${kompoz_install_default})
option(KOMPOZ_BUILD_EXAMPLES "Build the example apps (needs the platform layer)" ON)
option(KOMPOZ_VENDORED_DEPS "Build FreeType, HarfBuzz, zlib and libpng from third_party/ (run scripts/fetch-deps.sh)" ${kompoz_vendored})
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
    src/curves.cpp
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
    src/color.cpp
    src/colorspace.cpp
    src/path_effect.cpp
    src/image_bitmap.cpp
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
    # KOMPOZ_ROUTE needs the conforming preprocessor (__VA_OPT__), in apps too; the sources and the
    # headers are UTF-8 (U'İ', "…"), which MSVC reads as the system's code page without /utf-8.
    target_compile_options(kompoz_core PUBLIC /Zc:preprocessor /utf-8)
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

if(KOMPOZ_BUILD_APP)
    if(KOMPOZ_VENDORED_DEPS)
        foreach(dep freetype zlib libpng)
            if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/${dep}/CMakeLists.txt)
                message(FATAL_ERROR "third_party/${dep} is missing. Run scripts/fetch-deps.sh first.")
            endif()
        endforeach()

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
        # zlib and libpng (PNG read and written), static libraries of their sources: zlib's deflate
        # and inflate, libpng without its SIMD code (the plain C, the same everywhere).
        set(kompoz_zlib_dir ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib)
        add_library(kompoz_zlib STATIC)
        foreach(src adler32 crc32 deflate inffast inflate inftrees trees zutil)
            target_sources(kompoz_zlib PRIVATE ${kompoz_zlib_dir}/${src}.c)
        endforeach()
        target_include_directories(kompoz_zlib PUBLIC $<BUILD_INTERFACE:${kompoz_zlib_dir}>)
        set_target_properties(kompoz_zlib PROPERTIES EXPORT_NAME zlib)  # (in Kompoz's package)
        set(kompoz_png_dir ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libpng)
        configure_file(${kompoz_png_dir}/scripts/pnglibconf.h.prebuilt ${CMAKE_CURRENT_BINARY_DIR}/libpng/pnglibconf.h COPYONLY)
        add_library(kompoz_png STATIC)
        foreach(src png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans pngwio pngwrite pngwtran pngwutil)
            target_sources(kompoz_png PRIVATE ${kompoz_png_dir}/${src}.c)
        endforeach()
        target_include_directories(kompoz_png PUBLIC $<BUILD_INTERFACE:${kompoz_png_dir}> $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/libpng>)
        target_compile_definitions(kompoz_png PRIVATE PNG_ARM_NEON_OPT=0 PNG_INTEL_SSE_OPT=0 PNG_MIPS_MSA_OPT=0 PNG_MIPS_MMI_OPT=0
                                   PNG_POWERPC_VSX_OPT=0 PNG_LOONGARCH_LSX_OPT=0 PNG_RISCV_RVV_OPT=0)
        set_target_properties(kompoz_png PROPERTIES EXPORT_NAME png)
        target_link_libraries(kompoz_png PUBLIC kompoz_zlib)
        add_library(PNG::PNG ALIAS kompoz_png)
    else()
        find_package(Freetype REQUIRED)
        find_package(harfbuzz CONFIG QUIET)
        find_package(PNG REQUIRED)
    endif()

    # An OBJECT library because the platform defines the entry point (main, the activity's native
    # methods on Android), which must always end up in the final binary.
    if(ANDROID)
        set(kompoz_platform_source platform/android/android_platform.cpp)
    else()
        set(kompoz_platform_source platform/main.cpp)
    endif()
    add_library(kompoz_app OBJECT platform/app/app_core.cpp platform/app/title_bar.cpp platform/common/system.cpp ${kompoz_platform_source} platform/common/font_system.cpp
                                   platform/common/image_loader.cpp platform/common/image_codec.cpp)
    target_link_libraries(kompoz_app PUBLIC kompoz_core Freetype::Freetype PNG::PNG)
    set_target_properties(kompoz_app PROPERTIES EXPORT_NAME app)
    add_library(kompoz::app ALIAS kompoz_app)
    # Images (AsyncImage): Android, Windows and the web decode and download with the platform's
    # own; Linux and macOS with libjpeg-turbo, libwebp and libcurl (cmake/KompozImageLibraries.cmake).
    if(WIN32)
        target_link_libraries(kompoz_app PUBLIC windowscodecs ole32 wininet)
        target_compile_definitions(kompoz_app PRIVATE NOMINMAX)  # (windows.h's min and max macros break std::min/max)
    elseif(NOT ANDROID AND NOT EMSCRIPTEN)
        include(cmake/KompozImageLibraries.cmake)
        set(kompoz_image_libraries)
        foreach(lib JPEG WEBP CURL)
            set(KOMPOZ_WITH_${lib} AUTO CACHE STRING "AsyncImage's ${lib} library (Linux, macOS): AUTO (when found), ON (required) or OFF")
            set_property(CACHE KOMPOZ_WITH_${lib} PROPERTY STRINGS AUTO ON OFF)
            kompoz_find_image_library(${lib} ${KOMPOZ_WITH_${lib}})
            if(KOMPOZ_${lib}_TARGET)
                target_link_libraries(kompoz_app PUBLIC ${KOMPOZ_${lib}_TARGET})
                list(APPEND kompoz_image_libraries ${lib})
            endif()
        endforeach()
        if(KOMPOZ_JPEG_TARGET)
            target_compile_definitions(kompoz_app PRIVATE KOMPOZ_TURBOJPEG=1)
            if(KOMPOZ_JPEG_VERSION VERSION_GREATER_EQUAL 3.0)
                target_compile_definitions(kompoz_app PRIVATE KOMPOZ_TURBOJPEG3=1)
            endif()
        endif()
        if(KOMPOZ_WEBP_TARGET)
            target_compile_definitions(kompoz_app PRIVATE KOMPOZ_WEBP=1)
        endif()
        if(KOMPOZ_CURL_TARGET)
            target_compile_definitions(kompoz_app PRIVATE KOMPOZ_CURL=1)
        endif()
        message(STATUS "Kompoz: images: JPEG ${KOMPOZ_JPEG_HOW} ${KOMPOZ_JPEG_VERSION}, WebP ${KOMPOZ_WEBP_HOW}, HTTP ${KOMPOZ_CURL_HOW} (libjpeg-turbo, libwebp, libcurl; empty: not used)")
    endif()
    # The GPU renderer (platform/gpu): its core everywhere, and the Vulkan backend wherever Vulkan
    # is a GPU API (Linux, Android, Windows). Vulkan's headers come from the system or the package
    # manager, else from third_party/ (scripts/fetch-deps.sh); the Vulkan Memory Allocator is in
    # platform/gpu/external/. Neither is needed by apps: Vulkan itself is loaded at run time.
    # The OpenGL ES 3 backend needs nothing to build: the context is the platform's, its functions
    # looked up at run time (on the web WebGL 2's, which apps are linked for).
    target_sources(kompoz_app PRIVATE platform/gpu/device.cpp platform/gpu/path_geometry.cpp platform/gpu/renderer.cpp
                   platform/gpu/gles_device.cpp)
    target_compile_definitions(kompoz_app PRIVATE KOMPOZ_GPU_GLES=1)
    # On the web the WebGPU backend too: Dawn's webgpu.h for Emscripten (its emdawnwebgpu port), and
    # ASYNCIFY, through which a readback waits for the GPU (WebGPU's mapping is asynchronous).
    if(EMSCRIPTEN)
        target_sources(kompoz_app PRIVATE platform/gpu/webgpu_device.cpp platform/web/web_platform.cpp)  # (and Kompoz's own web platform)
        target_compile_definitions(kompoz_app PRIVATE KOMPOZ_WEB=1)
        set_source_files_properties(platform/gpu/webgpu_device.cpp PROPERTIES COMPILE_OPTIONS --use-port=emdawnwebgpu)
        target_compile_definitions(kompoz_app PRIVATE KOMPOZ_GPU_WEBGPU=1)
        target_link_options(kompoz_app INTERFACE -sMAX_WEBGL_VERSION=2 --use-port=emdawnwebgpu -sASYNCIFY)
    endif()
    # The Metal backend on Apple's systems: Objective-C++ (with ARC) over the system's frameworks.
    if(APPLE)
        enable_language(OBJCXX)
        target_sources(kompoz_app PRIVATE platform/gpu/metal_device.mm)
        set_source_files_properties(platform/gpu/metal_device.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
        target_compile_definitions(kompoz_app PRIVATE KOMPOZ_GPU_METAL=1)
        target_link_libraries(kompoz_app PUBLIC "-framework Metal" "-framework QuartzCore" "-framework Foundation")
        # Kompoz's macOS platform (platform/macos).
        if(NOT IOS)
            target_sources(kompoz_app PRIVATE platform/macos/macos_platform.mm)
            set_source_files_properties(platform/macos/macos_platform.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
            target_compile_definitions(kompoz_app PRIVATE KOMPOZ_MACOS=1)
            target_link_libraries(kompoz_app PUBLIC "-framework AppKit" "-framework IOKit" "-framework AVFoundation")
        endif()
    endif()
    if(NOT APPLE AND NOT EMSCRIPTEN)
        find_package(VulkanHeaders CONFIG QUIET)
        # (find_path searches where the toolchain's headers are: the NDK's sysroot, MinGW's, the
        # system's, not the build machine's when cross-compiling.)
        find_path(KOMPOZ_VULKAN_INCLUDE_DIR vulkan/vulkan.h)
        if(TARGET Vulkan::Headers)
            get_target_property(kompoz_vulkan_include Vulkan::Headers INTERFACE_INCLUDE_DIRECTORIES)
        elseif(KOMPOZ_VULKAN_INCLUDE_DIR)
            set(kompoz_vulkan_include ${KOMPOZ_VULKAN_INCLUDE_DIR})
        elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Headers/include/vulkan/vulkan.h)
            set(kompoz_vulkan_include ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Headers/include)
        else()
            message(FATAL_ERROR "Kompoz's Vulkan backend needs Vulkan's headers: install them (vulkan-headers, libvulkan-dev) "
                                "or run scripts/fetch-deps.sh")
        endif()
        target_sources(kompoz_app PRIVATE platform/gpu/vulkan_device.cpp platform/gpu/vma.cpp)
        target_include_directories(kompoz_app PRIVATE $<BUILD_INTERFACE:${kompoz_vulkan_include}>
                                   $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/platform/gpu/external>)
        target_compile_definitions(kompoz_app PRIVATE KOMPOZ_GPU_VULKAN=1)
        set(kompoz_gpu_vulkan ON)
        set_source_files_properties(platform/gpu/vma.cpp PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU,Clang>:-w>")  # (its warnings)
    endif()
    # Kompoz's platforms on Linux: Wayland's (platform/linux/wayland) in a
    # Wayland session, built where Wayland's and xkbcommon's headers are (the protocols' code is
    # generated ahead, scripts/wayland-protocols.sh), and X11's (platform/linux/x11), built where
    # X11's, XInput 2's, Xcursor's, XRandR's and xkbcommon's headers are. They are linked to none of
    # those libraries, nor to EGL: they are loaded at run time.
    if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
        find_path(KOMPOZ_WAYLAND_INCLUDE_DIR wayland-client-core.h PATH_SUFFIXES wayland)
        find_path(KOMPOZ_XKBCOMMON_INCLUDE_DIR xkbcommon/xkbcommon.h)
        find_path(KOMPOZ_X11_INCLUDE_DIR X11/Xlib.h)
        find_path(KOMPOZ_XI_INCLUDE_DIR X11/extensions/XInput2.h)
        find_path(KOMPOZ_XCURSOR_INCLUDE_DIR X11/Xcursor/Xcursor.h)
        find_path(KOMPOZ_XRANDR_INCLUDE_DIR X11/extensions/Xrandr.h)
        set(kompoz_linux_platform OFF)
        if(KOMPOZ_WAYLAND_INCLUDE_DIR AND KOMPOZ_XKBCOMMON_INCLUDE_DIR)
            file(GLOB kompoz_wayland_protocols CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/platform/linux/wayland/generated/*.c)
            target_sources(kompoz_app PRIVATE platform/linux/wayland/wayland_platform.cpp platform/linux/wayland/wayland_dynamic.cpp
                           ${kompoz_wayland_protocols})
            target_include_directories(kompoz_app PRIVATE ${KOMPOZ_WAYLAND_INCLUDE_DIR})
            target_compile_definitions(kompoz_app PRIVATE KOMPOZ_WAYLAND=1)
            set(kompoz_linux_platform ON)
        else()
            message(STATUS "Kompoz: Wayland's or xkbcommon's headers not found; Wayland sessions run on X11's platform (XWayland)")
        endif()
        if(KOMPOZ_X11_INCLUDE_DIR AND KOMPOZ_XI_INCLUDE_DIR AND KOMPOZ_XCURSOR_INCLUDE_DIR AND KOMPOZ_XRANDR_INCLUDE_DIR
           AND KOMPOZ_XKBCOMMON_INCLUDE_DIR)
            target_sources(kompoz_app PRIVATE platform/linux/x11/x11_platform.cpp platform/linux/x11/x11_dynamic.cpp)
            target_include_directories(kompoz_app PRIVATE ${KOMPOZ_X11_INCLUDE_DIR} ${KOMPOZ_XI_INCLUDE_DIR} ${KOMPOZ_XCURSOR_INCLUDE_DIR}
                                       ${KOMPOZ_XRANDR_INCLUDE_DIR})
            target_compile_definitions(kompoz_app PRIVATE KOMPOZ_X11=1)
            set(kompoz_linux_platform ON)
        else()
            message(STATUS "Kompoz: X11's, XInput 2's, Xcursor's, XRandR's or xkbcommon's headers not found; no X11 platform")
        endif()
        if(NOT kompoz_linux_platform)
            message(FATAL_ERROR "Kompoz needs Wayland's and xkbcommon's headers, or X11's, XInput 2's, Xcursor's, XRandR's and "
                                "xkbcommon's, for a platform to run apps on (see docs/guide/en/setup.md), or -DKOMPOZ_BUILD_APP=OFF")
        endif()
        if(kompoz_linux_platform)
            target_sources(kompoz_app PRIVATE platform/linux/desktop_services.cpp platform/common/egl_window.cpp platform/linux/media_devices.cpp)
            target_include_directories(kompoz_app PRIVATE ${KOMPOZ_XKBCOMMON_INCLUDE_DIR})
        endif()
    endif()
    # Text is shaped with HarfBuzz when it is there: ligatures, kerning, Arabic joining, Indic
    # scripts, emoji sequences.
    if(TARGET harfbuzz::harfbuzz)
        target_link_libraries(kompoz_app PUBLIC harfbuzz::harfbuzz)
        target_compile_definitions(kompoz_app PRIVATE KOMPOZ_HARFBUZZ=1)
    else()
        message(STATUS "Kompoz: HarfBuzz not found; text is drawn character by character (no ligatures, joining or emoji sequences)")
    endif()
    if(ANDROID)
        target_sources(kompoz_app PRIVATE platform/android/android_ime.cpp  # + platform/android/java
                                           platform/android/android_performance.cpp)
        target_link_libraries(kompoz_app PUBLIC android log EGL ${CMAKE_DL_LIBS})
    elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
        # The desktop's accent color from the XDG portal (libdbus loaded at run time): dynamic color.
        # Screen readers read the semantics tree through AT-SPI (the same libdbus).
        target_sources(kompoz_app PRIVATE platform/linux/desktop_accent.cpp platform/linux/atspi_bridge.cpp)
        target_compile_definitions(kompoz_app PRIVATE KOMPOZ_DESKTOP_ACCENT=1 KOMPOZ_ATSPI=1)
        target_link_libraries(kompoz_app PUBLIC ${CMAKE_DL_LIBS})
    elseif(WIN32)
        # Windows' accent color (DWM's, in the registry): dynamic color. Screen readers read the
        # semantics tree through UI Automation. Kompoz's Win32 platform (platform/windows).
        target_sources(kompoz_app PRIVATE platform/windows/desktop_accent.cpp platform/windows/uia_bridge.cpp
                       platform/windows/win32_platform.cpp platform/common/egl_window.cpp)
        target_compile_definitions(kompoz_app PRIVATE KOMPOZ_DESKTOP_ACCENT=1 KOMPOZ_UIA=1 KOMPOZ_WIN32=1)
        target_link_libraries(kompoz_app PUBLIC uiautomationcore oleaut32 dwmapi imm32 winmm cfgmgr32 shell32 uuid)
    endif()
    # Kompoz's headless platform on the desktops (KOMPOZ_HEADLESS=1: tests, scripted runs, CI).
    if(NOT kompoz_mobile)
        target_sources(kompoz_app PRIVATE platform/headless/headless.cpp platform/headless/headless_gpu.cpp)
        target_compile_definitions(kompoz_app PRIVATE KOMPOZ_HEADLESS=1)
    endif()
    if(WIN32 OR (UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN))
        # The wallpaper's own colors, found and picked on a thread (Android's come from the system).
        target_sources(kompoz_app PRIVATE platform/common/wallpaper_colors.cpp)
        target_compile_definitions(kompoz_app PRIVATE KOMPOZ_WALLPAPER_COLORS=1)
    endif()
    # The platform code that runs without a window (the wallpaper's colors, AT-SPI; UI Automation on
    # Windows), tested on its own.
    if(KOMPOZ_BUILD_TESTS AND UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
        add_executable(kompoz_platform_tests tests/platform_tests.cpp platform/common/wallpaper_colors.cpp
                       platform/common/image_loader.cpp platform/common/image_codec.cpp platform/linux/atspi_bridge.cpp
                       platform/common/system.cpp)
        target_link_libraries(kompoz_platform_tests PRIVATE $<TARGET_PROPERTY:kompoz_app,INTERFACE_LINK_LIBRARIES>)
        target_compile_definitions(kompoz_platform_tests PRIVATE $<TARGET_PROPERTY:kompoz_app,COMPILE_DEFINITIONS>)
        add_test(NAME kompoz_platform_tests COMMAND kompoz_platform_tests)
    elseif(KOMPOZ_BUILD_TESTS AND WIN32)
        add_executable(kompoz_platform_tests tests/platform_tests_windows.cpp platform/windows/uia_bridge.cpp)
        target_compile_definitions(kompoz_platform_tests PRIVATE NOMINMAX)
        target_link_libraries(kompoz_platform_tests PRIVATE $<TARGET_PROPERTY:kompoz_app,INTERFACE_LINK_LIBRARIES>)
        add_test(NAME kompoz_platform_tests COMMAND kompoz_platform_tests)
    endif()

    # For an app's CMakeLists: copies what every app ships (Kompoz's assets/: the bundled fonts and
    # the licenses) next to `target`'s binary each time it is built (desktops; Android and the web
    # take assets/ into the APK and the page's data, as the showcase does).
    function(kompoz_copy_assets target)
        add_custom_command(TARGET ${target} POST_BUILD
                           COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/assets $<TARGET_FILE_DIR:${target}>)
    endfunction()

    # The GPU renderer drawn on a real device (Vulkan or Metal, or OpenGL ES with KOMPOZ_GPU=gles), against
    # pictures in tests/golden/. On Android (KOMPOZ_BUILD_TESTS=ON with the NDK's toolchain) a
    # program run from adb shell, on a phone's own GPU; on the web (with Emscripten's) a page,
    # kompoz_gpu_tests.html, on the browser's WebGL 2: its fonts and the pictures in its data, its
    # query its environment (as the showcase's), and emrun to open it in a browser and report what it
    # printed and how it ended:
    #   emrun --browser chrome build-web/kompoz_gpu_tests.html KOMPOZ_GPU_REQUIRED=1
    if(KOMPOZ_BUILD_TESTS AND (kompoz_gpu_vulkan OR EMSCRIPTEN OR APPLE))
        add_executable(kompoz_gpu_tests tests/gpu_tests.cpp platform/gpu/device.cpp platform/gpu/path_geometry.cpp platform/gpu/renderer.cpp
                       platform/gpu/gles_device.cpp platform/common/font_system.cpp platform/common/image_codec.cpp platform/common/system.cpp)
        if(kompoz_gpu_vulkan)
            target_sources(kompoz_gpu_tests PRIVATE platform/gpu/vulkan_device.cpp platform/gpu/vma.cpp)
            target_include_directories(kompoz_gpu_tests PRIVATE ${kompoz_vulkan_include} platform/gpu/external)
            set_source_files_properties(platform/gpu/vma.cpp TARGET_DIRECTORY kompoz_gpu_tests PROPERTIES COMPILE_OPTIONS
                                        "$<$<CXX_COMPILER_ID:GNU,Clang>:-w>")
        endif()
        if(APPLE)
            target_sources(kompoz_gpu_tests PRIVATE platform/gpu/metal_device.mm)
        endif()
        if(EMSCRIPTEN)
            target_sources(kompoz_gpu_tests PRIVATE platform/gpu/webgpu_device.cpp)
        else()
            target_sources(kompoz_gpu_tests PRIVATE platform/headless/headless_gpu.cpp)
        endif()
        if(ANDROID)
            target_sources(kompoz_gpu_tests PRIVATE platform/android/android_ime.cpp)  # (the font system's glyphs Android draws)
        endif()
        target_link_libraries(kompoz_gpu_tests PRIVATE $<TARGET_PROPERTY:kompoz_app,INTERFACE_LINK_LIBRARIES>)
        target_link_options(kompoz_gpu_tests PRIVATE $<TARGET_PROPERTY:kompoz_app,INTERFACE_LINK_OPTIONS>)
        target_compile_definitions(kompoz_gpu_tests PRIVATE $<TARGET_PROPERTY:kompoz_app,COMPILE_DEFINITIONS>)
        if(EMSCRIPTEN)
            set_target_properties(kompoz_gpu_tests PROPERTIES SUFFIX ".html")
            target_compile_definitions(kompoz_gpu_tests PRIVATE KOMPOZ_GOLDEN_DIR="/golden")
            target_link_options(kompoz_gpu_tests PRIVATE -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sEXIT_RUNTIME=1
                                -sEXPORTED_RUNTIME_METHODS=ENV --emrun
                                "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/platform/web/shell.html"
                                "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts@/fonts"
                                "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden@/golden")
        else()
            target_compile_definitions(kompoz_gpu_tests PRIVATE KOMPOZ_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
            kompoz_copy_assets(kompoz_gpu_tests)
            add_test(NAME kompoz_gpu_tests COMMAND kompoz_gpu_tests WORKING_DIRECTORY $<TARGET_FILE_DIR:kompoz_gpu_tests>)
        endif()
    endif()

    if(KOMPOZ_BUILD_EXAMPLES)
        add_subdirectory(examples/showcase)
    endif()
endif()

# cmake --install: Kompoz's headers, kompoz_core, the platform layer's objects (the entry point) and
# the assets, and a CMake package: find_package(kompoz) gives kompoz::core, kompoz::app and
# kompoz_copy_assets. FreeType built here is installed beside it with its own package, HarfBuzz,
# zlib and libpng built here in Kompoz's; the system's are found again where they were.
if(KOMPOZ_INSTALL)
    include(CMakePackageConfigHelpers)
    set(kompoz_cmake_dir ${CMAKE_INSTALL_LIBDIR}/cmake/kompoz)
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(TARGETS kompoz_core EXPORT kompozTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    set(KOMPOZ_PACKAGE_APP OFF)
    if(TARGET kompoz_app)
        set(KOMPOZ_PACKAGE_APP ON)
        install(TARGETS kompoz_app EXPORT kompozTargets OBJECTS DESTINATION ${CMAKE_INSTALL_LIBDIR}/kompoz)
        if(TARGET kompoz_harfbuzz)
            install(TARGETS kompoz_harfbuzz EXPORT kompozTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        endif()
        if(TARGET kompoz_png)
            install(TARGETS kompoz_png kompoz_zlib EXPORT kompozTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        endif()
        install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/kompoz/assets)
    endif()
    install(EXPORT kompozTargets NAMESPACE kompoz:: DESTINATION ${kompoz_cmake_dir})
    # What the package finds again: kompoz_app's dependencies, the way they were found here.
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
