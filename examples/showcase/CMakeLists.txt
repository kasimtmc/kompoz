# The showcase: a desktop executable, or libmain.so for the Android app in android/
# (dev.kompoz.KompozActivity loads it).
# A folder per screen, each screen's sections in files of their own (showcase.hpp declares what
# they share), so that an edit rebuilds only its file.
set(showcase_sources main.cpp common.cpp home/home.cpp list/list.cpp places/places.cpp
    components/components.cpp components/state.cpp components/layout.cpp components/scroll.cpp components/graphics.cpp
    components/drawing.cpp components/input.cpp components/text.cpp
    material/material.cpp material/sections.cpp material/text_fields.cpp material/controls.cpp material/containers.cpp
    material/navigation.cpp)
if(ANDROID)
    add_library(main SHARED ${showcase_sources})
    target_include_directories(main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(main PRIVATE kompoz_sdl3)
else()
    add_executable(showcase ${showcase_sources})
    target_include_directories(showcase PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(showcase PRIVATE kompoz_sdl3)
    set_target_properties(showcase PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})  # ./build/showcase
    file(GLOB_RECURSE showcase_assets CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/assets/* ${CMAKE_CURRENT_SOURCE_DIR}/assets/*)
    if(EMSCRIPTEN)
        # showcase.html (+ .js, .wasm, .data): the fonts and images go in its virtual file system;
        # the page (platform/web/shell.html) is just the canvas, its query the app's environment
        # (showcase.html?KOMPOZ_GPU=gles), which it sets through ENV.
        set_target_properties(showcase PROPERTIES SUFFIX ".html")
        target_link_options(showcase PRIVATE -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sEXPORTED_RUNTIME_METHODS=ENV
                            "--shell-file=${PROJECT_SOURCE_DIR}/platform/web/shell.html"
                            "--preload-file=${PROJECT_SOURCE_DIR}/assets@/"
                            "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/")
        set_property(TARGET showcase APPEND PROPERTY LINK_DEPENDS ${showcase_assets})  # an edited asset is packed again
        return()
    endif()
    if(MINGW)  # no MinGW runtime DLLs to ship either
        target_link_options(showcase PRIVATE -static)
    endif()
    # The library's bundled fonts and the showcase's images and vectors next to the binary, as the
    # mobile builds ship them in their assets: copied on every build, so an edited one is there
    # without the binary being linked again.
    add_custom_target(showcase_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets ${PROJECT_BINARY_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets ${PROJECT_BINARY_DIR}
        SOURCES ${showcase_assets})
    add_dependencies(showcase showcase_assets)
endif()
