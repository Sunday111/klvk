cmake_minimum_required(VERSION 3.20)
include(set_compiler_options)
set(module_source_files
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/post_processing_example.cpp)
add_executable(klvk_post_processing_example ${module_source_files})
set_generic_compiler_options(klvk_post_processing_example PRIVATE)
target_link_libraries(klvk_post_processing_example PUBLIC klvk)
target_include_directories(klvk_post_processing_example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/code/public)
target_include_directories(klvk_post_processing_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/code/private)
set(klvk_post_processing_example_copy_files_outputs)
file(GLOB_RECURSE klvk_post_processing_example_copy_files_sources_0 CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/content/*")
foreach(source_file ${klvk_post_processing_example_copy_files_sources_0})
    file(RELATIVE_PATH rel_path "${CMAKE_CURRENT_SOURCE_DIR}/content" "${source_file}")
    set(copied_file "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/content/${rel_path}")
    add_custom_command(
        OUTPUT "${copied_file}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_newer "${source_file}" "${copied_file}"
        DEPENDS "${source_file}"
        COMMENT "stage ${rel_path}")
    list(APPEND klvk_post_processing_example_copy_files_outputs "${copied_file}")
endforeach()
add_custom_target(klvk_post_processing_example_copy_files ALL DEPENDS ${klvk_post_processing_example_copy_files_outputs})
add_dependencies(klvk_post_processing_example klvk_post_processing_example_copy_files)
