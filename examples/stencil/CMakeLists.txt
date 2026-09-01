cmake_minimum_required(VERSION 3.29)
include(set_compiler_options)
include(yae_debug_info_compression)
set(module_source_files
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/stencil_example.cpp)
add_executable(klvk_stencil_example ${module_source_files})
set_generic_compiler_options(klvk_stencil_example PRIVATE)
target_link_libraries(klvk_stencil_example PUBLIC klvk)
target_include_directories(klvk_stencil_example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/code/public)
target_include_directories(klvk_stencil_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/code/private)
enable_debug_info_compression_for(klvk_stencil_example)
add_custom_target(klvk_stencil_example_copy_files ALL
    COMMAND ${Python3_EXECUTABLE} "${YAE_SUPPORT_ROOT}/scripts/stage_directories.py"
            --destination "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/content"
            --destination-root "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            --source "${CMAKE_CURRENT_SOURCE_DIR}/content"
            --manifest "${YAE_STAGING_ROOT}/content/klvk_stencil_example_copy_files.manifest"
            --active-manifests "${YAE_STAGING_PLAN}"
    COMMENT "stage content of klvk_stencil_example"
    VERBATIM)
add_dependencies(klvk_stencil_example_copy_files yae_reconcile_staging)
add_dependencies(klvk_stencil_example klvk_stencil_example_copy_files)
