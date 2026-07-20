cmake_minimum_required(VERSION 3.20)
include(set_compiler_options)
set(module_source_files
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/diagnostic_config_test.cpp)
add_executable(klvk_diagnostic_config_test ${module_source_files})
set_generic_compiler_options(klvk_diagnostic_config_test PRIVATE)
target_link_libraries(klvk_diagnostic_config_test PUBLIC klvk)
target_include_directories(klvk_diagnostic_config_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/code/public)
target_include_directories(klvk_diagnostic_config_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/code/private)
