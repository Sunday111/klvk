# Kept under the old function name so existing modules need no migration.
# GLSL is compiled at runtime; the build only stages sources and configs.
if(NOT COMMAND klvk_compile_shaders)
    function(klvk_compile_shaders target)
        if(TARGET ${target}_compile_shaders)
            return()
        endif()

        get_target_property(module_dir ${target} SOURCE_DIR)
        set(shaders_src_dir "${module_dir}/shaders")
        set(shaders_out_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/content/shaders")
        file(GLOB_RECURSE shader_files
            "${shaders_src_dir}/*.vert"
            "${shaders_src_dir}/*.geom"
            "${shaders_src_dir}/*.frag"
            "${shaders_src_dir}/*.comp"
            "${shaders_src_dir}/*.tesc"
            "${shaders_src_dir}/*.tese"
            "${shaders_src_dir}/*.shader.json")

        set(staged_outputs)
        foreach(shader_file ${shader_files})
            file(RELATIVE_PATH shader_rel "${shaders_src_dir}" "${shader_file}")
            set(shader_output "${shaders_out_dir}/${shader_rel}")
            get_filename_component(shader_output_dir "${shader_output}" DIRECTORY)
            add_custom_command(
                OUTPUT "${shader_output}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${shader_output_dir}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${shader_file}" "${shader_output}"
                DEPENDS "${shader_file}"
                COMMENT "stage shader ${shader_rel}")
            list(APPEND staged_outputs "${shader_output}")
        endforeach()

        add_custom_target(${target}_compile_shaders DEPENDS ${staged_outputs})
        add_dependencies(${target} ${target}_compile_shaders)
    endfunction()
endif()

if(TARGET klvk)
    klvk_compile_shaders(klvk)
endif()
