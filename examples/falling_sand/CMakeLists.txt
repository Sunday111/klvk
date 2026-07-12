cmake_minimum_required(VERSION 3.20)
include(set_compiler_options)
set(module_source_files
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/falling_sand_example.cpp)
add_executable(klvk_falling_sand_example ${module_source_files})
set_generic_compiler_options(klvk_falling_sand_example PRIVATE)
target_link_libraries(klvk_falling_sand_example PUBLIC klvk)
target_include_directories(klvk_falling_sand_example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/code/public)
target_include_directories(klvk_falling_sand_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/code/private)
