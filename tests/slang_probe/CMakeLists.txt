cmake_minimum_required(VERSION 3.20)
include(set_compiler_options)
set(module_source_files
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/main.cpp)
add_executable(klvk_slang_probe ${module_source_files})
set_generic_compiler_options(klvk_slang_probe PRIVATE)
target_link_libraries(klvk_slang_probe PUBLIC slang::slang)
target_include_directories(klvk_slang_probe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/code/public)
target_include_directories(klvk_slang_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/code/private)
