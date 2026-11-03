# GLSL -> SPIR-V at build time with the fetched glslang compiler (no Vulkan SDK needed).
# Each shader becomes a generated header "<name>_spv.h" holding `const uint32_t <name>_spv[]`.
# The FSR 1, SMAA and FXAA shader headers (AC04_FSR1_INCLUDE_DIR, AC04_SMAA_DIR, AC04_FXAA_DIR,
# cmake/Dependencies.cmake) are on the include path.

function(ac04_add_shaders target)
    set(out_dir "${CMAKE_BINARY_DIR}/generated/shaders")
    file(MAKE_DIRECTORY "${out_dir}")
    set(headers)
    foreach(src IN LISTS ARGN)
        get_filename_component(abs "${src}" ABSOLUTE)
        get_filename_component(name "${src}" NAME)
        string(REPLACE "." "_" var "${name}")
        set(header "${out_dir}/${var}_spv.h")
        # FXAA3_11.h (third party) names its macros with double underscores, which glslang warns
        # about on every line (not silenced by --suppress-warnings): that shader goes through
        # cmake/RunGlslangQuiet.cmake, which drops exactly those warnings.
        set(args -V --target-env vulkan1.2 --vn ${var}_spv "-I${AC04_FSR1_INCLUDE_DIR}" "-I${AC04_SMAA_DIR}"
                 "-I${AC04_FXAA_DIR}" -o "${header}" "${abs}")
        if(name STREQUAL "fxaa.frag")
            string(REPLACE ";" "|" quoted "${args}")
            set(command ${CMAKE_COMMAND} "-DGLSLANG=$<TARGET_FILE:glslang-standalone>" "-DARGS=${quoted}"
                        -P "${CMAKE_SOURCE_DIR}/cmake/RunGlslangQuiet.cmake")
        else()
            set(command $<TARGET_FILE:glslang-standalone> ${args})
        endif()
        add_custom_command(
            OUTPUT "${header}"
            COMMAND ${command}
            DEPENDS "${abs}" glslang-standalone "${AC04_FSR1_INCLUDE_DIR}/ffx_a.h" "${AC04_FSR1_INCLUDE_DIR}/ffx_fsr1.h"
                    "${AC04_SMAA_DIR}/SMAA.hlsl" "${AC04_FXAA_DIR}/FXAA3_11.h"
                    "${CMAKE_SOURCE_DIR}/cmake/RunGlslangQuiet.cmake"
            COMMENT "Compiling shader ${name}"
            VERBATIM)
        list(APPEND headers "${header}")
    endforeach()
    add_custom_target(${target}_shaders DEPENDS ${headers})
    add_dependencies(${target} ${target}_shaders)
    target_include_directories(${target} PRIVATE "${out_dir}")
endfunction()

# A second build of one shader with a preprocessor define: `<out_name>_spv.h` holding
# `const uint32_t <out_name>_spv[]` (e.g. gs.frag with -DGS_SINGLE_SOURCE as gs_single_frag).
function(ac04_add_shader_variant target src out_name define)
    set(out_dir "${CMAKE_BINARY_DIR}/generated/shaders")
    file(MAKE_DIRECTORY "${out_dir}")
    get_filename_component(abs "${src}" ABSOLUTE)
    set(header "${out_dir}/${out_name}_spv.h")
    add_custom_command(
        OUTPUT "${header}"
        COMMAND $<TARGET_FILE:glslang-standalone> -V --target-env vulkan1.2 "-D${define}" --vn ${out_name}_spv
                -o "${header}" "${abs}"
        DEPENDS "${abs}" glslang-standalone
        COMMENT "Compiling shader ${out_name} (${define})"
        VERBATIM)
    add_custom_target(${target}_${out_name} DEPENDS "${header}")
    add_dependencies(${target} ${target}_${out_name})
    target_include_directories(${target} PRIVATE "${out_dir}")
endfunction()
