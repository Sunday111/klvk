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
add_custom_target(klvk_fractal_example_copy_files ALL
    COMMAND ${Python3_EXECUTABLE} "${YAE_SUPPORT_ROOT}/scripts/stage_directories.py"
            --destination "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/content"
            --source "${CMAKE_CURRENT_SOURCE_DIR}/content"
            --manifest "${CMAKE_CURRENT_BINARY_DIR}/klvk_fractal_example_copy_files_content.manifest"
    COMMENT "stage content of klvk_fractal_example"
    VERBATIM)
add_dependencies(klvk_fractal_example klvk_fractal_example_copy_files)
