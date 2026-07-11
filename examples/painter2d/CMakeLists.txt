cmake_minimum_required(VERSION 3.20)
include(set_compiler_options)
set(module_source_files
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/painter2d_example.cpp)
add_executable(klvk_painter2d_example ${module_source_files})
set_generic_compiler_options(klvk_painter2d_example PRIVATE)
target_link_libraries(klvk_painter2d_example PUBLIC klvk)
target_include_directories(klvk_painter2d_example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/code/public)
target_include_directories(klvk_painter2d_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/code/private)
