cmake_minimum_required(VERSION 3.20)
include(set_compiler_options)
set(module_source_files
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/two_textures_example.cpp)
add_executable(klvk_two_textures_example ${module_source_files})
set_generic_compiler_options(klvk_two_textures_example PRIVATE)
target_link_libraries(klvk_two_textures_example PUBLIC klvk)
target_include_directories(klvk_two_textures_example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/code/public)
target_include_directories(klvk_two_textures_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/code/private)
add_custom_target(klvk_two_textures_example_copy_files ALL
    ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/content" ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/content
)
add_dependencies(klvk_two_textures_example klvk_two_textures_example_copy_files)
