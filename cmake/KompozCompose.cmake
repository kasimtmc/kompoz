# kompoz_compose(target): the target's C++ sources go through kompozc before the C++ compiler, so
# their @Composable functions can be written in Compose's own syntax:
#
#   @Composable
#   fun Greeting(name: String, modifier: Modifier = Modifier) {
#       Text("Merhaba $name", modifier = modifier.padding(16.dp))
#   }
#
# Call it after the target's sources are all added. The target's sources are one module, as a
# Kotlin module: a file calls another's functions with named arguments, no header needed. The C++
# compiler's messages point at the sources' own lines.
#
# kompozc runs on the machine that builds. Where Kompoz is built for another (Android, the web), it
# is built for this one from its sources, with this machine's C++ compiler; set KOMPOZC to a
# kompozc of yours instead (one built from Kompoz's compiler/, or installed with it).

# Kompoz's headers (kompozc reads their structs), and kompozc's sources: beside this file in
# Kompoz's tree; in an installed Kompoz, its include directory and compiler/ here.
if(NOT KOMPOZ_HEADERS_DIR)
    set(KOMPOZ_HEADERS_DIR ${CMAKE_CURRENT_LIST_DIR}/../include/kompoz)
    cmake_path(NORMAL_PATH KOMPOZ_HEADERS_DIR)
endif()
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/compiler/main.cpp)
    set(kompoz_compiler_dir ${CMAKE_CURRENT_LIST_DIR}/compiler)
else()
    set(kompoz_compiler_dir ${CMAKE_CURRENT_LIST_DIR}/../compiler)
    cmake_path(NORMAL_PATH kompoz_compiler_dir)
endif()
set(KOMPOZC "" CACHE FILEPATH "A kompozc for the machine that builds (empty: Kompoz's own, built where needed)")

# The kompozc to run, and what a translation depends on to have it built.
function(kompoz_kompozc command depends)
    if(KOMPOZC)
        set(${command} ${KOMPOZC} PARENT_SCOPE)
        set(${depends} ${KOMPOZC} PARENT_SCOPE)
    elseif(TARGET kompozc)  # built with Kompoz
        set(${command} kompozc PARENT_SCOPE)
        set(${depends} kompozc PARENT_SCOPE)
    elseif(TARGET kompoz::kompozc)  # installed with it
        set(${command} kompoz::kompozc PARENT_SCOPE)
        set(${depends} kompoz::kompozc PARENT_SCOPE)
    else()
        # Built for this machine on its own (not with the toolchain of the one built for).
        if(NOT TARGET kompozc_host)
            include(ExternalProject)
            set(dir ${CMAKE_BINARY_DIR}/kompozc-host)
            set(generator -G ${CMAKE_GENERATOR})
            if(CMAKE_MAKE_PROGRAM)
                list(APPEND generator -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM})
            endif()
            ExternalProject_Add(kompozc_host
                SOURCE_DIR ${kompoz_compiler_dir}
                BINARY_DIR ${dir}
                CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env --unset=CC --unset=CXX --unset=CFLAGS --unset=CXXFLAGS --unset=LDFLAGS
                                  ${CMAKE_COMMAND} ${generator} -DCMAKE_BUILD_TYPE=Release -S <SOURCE_DIR> -B <BINARY_DIR>
                BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release
                INSTALL_COMMAND ""
                BUILD_ALWAYS ON  # (its sources may change: their build sees that)
                BUILD_BYPRODUCTS ${dir}/kompozc${CMAKE_HOST_EXECUTABLE_SUFFIX})
            set_property(GLOBAL PROPERTY kompozc_host_executable ${dir}/kompozc${CMAKE_HOST_EXECUTABLE_SUFFIX})
        endif()
        get_property(executable GLOBAL PROPERTY kompozc_host_executable)
        set(${command} ${executable} PARENT_SCOPE)
        set(${depends} kompozc_host ${executable} PARENT_SCOPE)
    endif()
endfunction()

function(kompoz_compose target)
    kompoz_kompozc(kompozc kompozc_depends)
    get_target_property(sources ${target} SOURCES)
    get_target_property(source_dir ${target} SOURCE_DIR)
    set(out_dir ${CMAKE_CURRENT_BINARY_DIR}/${target}.kompozc)
    set(translated)
    # The module: every C++ source of the target, whose @Composable functions and data classes the
    # others call as Kotlin calls them across files.
    set(module)
    foreach(source IN LISTS sources)
        if(source MATCHES "\\.(cpp|cc|cxx)$" AND NOT source MATCHES "^\\$<")
            cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY ${source_dir} NORMALIZE OUTPUT_VARIABLE absolute)
            list(APPEND module ${absolute})
        endif()
    endforeach()
    set(module_args)
    foreach(m IN LISTS module)
        list(APPEND module_args --module ${m})
    endforeach()
    foreach(source IN LISTS sources)
        if(source MATCHES "\\.(cpp|cc|cxx)$" AND NOT source MATCHES "^\\$<")
            cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY ${source_dir} NORMALIZE OUTPUT_VARIABLE absolute)
            cmake_path(RELATIVE_PATH absolute BASE_DIRECTORY ${source_dir} OUTPUT_VARIABLE relative)
            string(REPLACE "../" "__/" relative "${relative}")  # (sources from outside the target's directory)
            set(output ${out_dir}/${relative})
            add_custom_command(OUTPUT ${output}
                COMMAND ${kompozc} ${absolute} -o ${output} --headers ${KOMPOZ_HEADERS_DIR} ${module_args} --depfile ${output}.d
                DEPENDS ${absolute} ${module} ${kompozc_depends}
                DEPFILE ${output}.d
                COMMENT "kompozc ${relative}"
                VERBATIM)
            list(APPEND translated ${output})
        else()
            list(APPEND translated ${source})
        endif()
    endforeach()
    set_property(TARGET ${target} PROPERTY SOURCES ${translated})
endfunction()
