cmake_minimum_required(VERSION 3.20)
include(set_compiler_options)
set(module_source_files
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/vulkan/vulkan_common.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/public/klvk/error_handling.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/public/klvk/vulkan/vulkan_common.hpp)
add_library(klvk STATIC ${module_source_files})
set_generic_compiler_options(klvk PRIVATE)
target_link_libraries(klvk PUBLIC edt
                                  fmt
                                  cpptrace::cpptrace
                                  magic_enum
                                  unordered_dense
                                  glfw
                                  Vulkan-Headers
                                  volk
                                  VulkanMemoryAllocator
                                  imgui_vulkan)
target_include_directories(klvk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/code/public)
target_include_directories(klvk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/code/private)
include(${CMAKE_CURRENT_SOURCE_DIR}/compile_shaders.cmake)
add_custom_target(klvk_copy_files ALL
    ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/content" ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/content
)
add_dependencies(klvk klvk_copy_files)
