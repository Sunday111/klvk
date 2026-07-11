# Included by generated CMake both from the module and the project directory, hence the guards
# and target-relative paths. Other modules (examples) reuse the function for their own shaders.
if(NOT COMMAND klvk_compile_shaders)
    find_program(KLVK_GLSLC glslc REQUIRED)

    # Compiles GLSL sources from <target source dir>/shaders next to the built binaries,
    # preserving relative paths: shaders/x/y.vert -> content/shaders/x/y.vert.spv.
    function(klvk_compile_shaders target)
        if(TARGET ${target}_compile_shaders)
            return()
        endif()

        get_target_property(module_dir ${target} SOURCE_DIR)
        set(shaders_src_dir "${module_dir}/shaders")
        set(shaders_out_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/content/shaders")

        file(GLOB_RECURSE shader_sources
            "${shaders_src_dir}/*.vert"
            "${shaders_src_dir}/*.geom"
            "${shaders_src_dir}/*.frag"
            "${shaders_src_dir}/*.comp")

        set(spirv_outputs)
        foreach(shader_source ${shader_sources})
            file(RELATIVE_PATH shader_rel "${shaders_src_dir}" "${shader_source}")
            set(shader_output "${shaders_out_dir}/${shader_rel}.spv")
            get_filename_component(shader_output_dir "${shader_output}" DIRECTORY)
            add_custom_command(
                OUTPUT "${shader_output}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${shader_output_dir}"
                COMMAND ${KLVK_GLSLC} --target-env=vulkan1.3 "${shader_source}" -o "${shader_output}"
                DEPENDS "${shader_source}"
                COMMENT "glslc ${shader_rel}")
            list(APPEND spirv_outputs "${shader_output}")
        endforeach()

        add_custom_target(${target}_compile_shaders DEPENDS ${spirv_outputs})
        add_dependencies(${target} ${target}_compile_shaders)
    endfunction()
endif()

if(TARGET klvk)
    klvk_compile_shaders(klvk)
endif()
