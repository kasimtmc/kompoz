# kompoz_compose(target): the target's C++ sources go through kompozc before the C++ compiler, so
# their @Composable functions can be written in Compose's own syntax:
#
#   @Composable
#   fun Greeting(name: String, modifier: Modifier = Modifier) {
#       Text("Merhaba $name", modifier = modifier.padding(16.dp))
#   }
#
# Call it after the target's sources are all added. The compiler's messages point at the
# sources' own lines.
# Kompoz's own headers: kompozc reads their structs (made with named arguments).
set(kompoz_headers ${CMAKE_CURRENT_LIST_DIR}/../include/kompoz)
cmake_path(NORMAL_PATH kompoz_headers)

function(kompoz_compose target)
    if(NOT TARGET kompozc)
        message(FATAL_ERROR "kompoz_compose(${target}): kompozc isn't built here (KOMPOZ_BUILD_COMPILER)")
    endif()
    get_target_property(sources ${target} SOURCES)
    get_target_property(source_dir ${target} SOURCE_DIR)
    set(out_dir ${CMAKE_CURRENT_BINARY_DIR}/${target}.kompozc)
    set(translated)
    foreach(source IN LISTS sources)
        if(source MATCHES "\\.(cpp|cc|cxx)$" AND NOT source MATCHES "^\\$<")
            cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY ${source_dir} NORMALIZE OUTPUT_VARIABLE absolute)
            cmake_path(RELATIVE_PATH absolute BASE_DIRECTORY ${source_dir} OUTPUT_VARIABLE relative)
            string(REPLACE "../" "__/" relative "${relative}")  # (sources from outside the target's directory)
            set(output ${out_dir}/${relative})
            add_custom_command(OUTPUT ${output}
                COMMAND kompozc ${absolute} -o ${output} --headers ${kompoz_headers} --depfile ${output}.d
                DEPENDS ${absolute} kompozc
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
