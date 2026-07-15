cmake_minimum_required(VERSION 3.20)
include(set_compiler_options)
set(module_source_files
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/clipboard.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/clipboard_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/clipboard_windows.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/fractal_app.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/fractal_app.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/fractal_example_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/fractal_settings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/fractal_settings.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/graphics_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/graphics_utils.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/interpolation_widget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/interpolation_widget.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/renderers/counting_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/renderers/counting_renderer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/renderers/fractal_renderer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/renderers/simple_cpu_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/renderers/simple_cpu_renderer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/renderers/simple_gpu_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/renderers/simple_gpu_renderer.hpp)
add_executable(klvk_fractal_example ${module_source_files})
set_generic_compiler_options(klvk_fractal_example PRIVATE)
target_link_libraries(klvk_fractal_example PUBLIC klvk)
target_include_directories(klvk_fractal_example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/code/public)
target_include_directories(klvk_fractal_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/code/private)
set(klvk_fractal_example_copy_files_outputs)
file(GLOB_RECURSE klvk_fractal_example_copy_files_sources_0 CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/content/*")
foreach(source_file ${klvk_fractal_example_copy_files_sources_0})
    file(RELATIVE_PATH rel_path "${CMAKE_CURRENT_SOURCE_DIR}/content" "${source_file}")
    set(copied_file "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/content/${rel_path}")
    add_custom_command(
        OUTPUT "${copied_file}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_newer "${source_file}" "${copied_file}"
        DEPENDS "${source_file}"
        COMMENT "stage ${rel_path}")
    list(APPEND klvk_fractal_example_copy_files_outputs "${copied_file}")
endforeach()
add_custom_target(klvk_fractal_example_copy_files ALL DEPENDS ${klvk_fractal_example_copy_files_outputs})
add_dependencies(klvk_fractal_example klvk_fractal_example_copy_files)
