cmake_minimum_required(VERSION 3.29)
include(set_compiler_options)
include(yae_debug_info_compression)
set(module_source_files
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/polygon_modes_test.cpp)
add_executable(klvk_polygon_modes_test ${module_source_files})
set_generic_compiler_options(klvk_polygon_modes_test PRIVATE)
target_link_libraries(klvk_polygon_modes_test PUBLIC klvk)
target_include_directories(klvk_polygon_modes_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/code/public)
target_include_directories(klvk_polygon_modes_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/code/private)
enable_debug_info_compression_for(klvk_polygon_modes_test)
add_custom_target(klvk_polygon_modes_test_copy_files ALL
    COMMAND ${Python3_EXECUTABLE} "${YAE_SUPPORT_ROOT}/scripts/stage_directories.py"
            --destination "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/content"
            --destination-root "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            --source "${CMAKE_CURRENT_SOURCE_DIR}/content"
            --manifest "${YAE_STAGING_ROOT}/content/klvk_polygon_modes_test_copy_files.manifest"
            --active-manifests "${YAE_STAGING_PLAN}"
    COMMENT "stage content of klvk_polygon_modes_test"
    VERBATIM)
add_dependencies(klvk_polygon_modes_test_copy_files yae_reconcile_staging)
add_dependencies(klvk_polygon_modes_test klvk_polygon_modes_test_copy_files)
