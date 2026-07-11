find_program(KLVK_GLSLC glslc REQUIRED)

set(klvk_shaders_src_dir "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(klvk_shaders_out_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/content/shaders")

file(GLOB_RECURSE klvk_shader_sources
    "${klvk_shaders_src_dir}/*.vert"
    "${klvk_shaders_src_dir}/*.frag"
    "${klvk_shaders_src_dir}/*.comp")

set(klvk_spirv_outputs)
foreach(shader_source ${klvk_shader_sources})
    file(RELATIVE_PATH shader_rel "${klvk_shaders_src_dir}" "${shader_source}")
    set(shader_output "${klvk_shaders_out_dir}/${shader_rel}.spv")
    get_filename_component(shader_output_dir "${shader_output}" DIRECTORY)
    add_custom_command(
        OUTPUT "${shader_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${shader_output_dir}"
        COMMAND ${KLVK_GLSLC} --target-env=vulkan1.3 "${shader_source}" -o "${shader_output}"
        DEPENDS "${shader_source}"
        COMMENT "glslc ${shader_rel}")
    list(APPEND klvk_spirv_outputs "${shader_output}")
endforeach()

add_custom_target(klvk_compile_shaders DEPENDS ${klvk_spirv_outputs})
add_dependencies(klvk klvk_compile_shaders)
