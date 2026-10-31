find_package(Vulkan REQUIRED COMPONENTS glslc)

# ---------------------------------------------------------------------------
# gpu-info (Fase 01)
# ---------------------------------------------------------------------------
add_executable(gpu-info
    src/main.cpp
    src/core/vk_util.cpp
    src/gpu/gpu_context.cpp
    src/gpu/gpu_selector.cpp
)

target_include_directories(gpu-info PRIVATE src)
target_link_libraries(gpu-info PRIVATE Vulkan::Vulkan)

if(MSVC)
    target_compile_options(gpu-info PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(gpu-info PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Shaders
# ---------------------------------------------------------------------------
set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

add_custom_command(
    OUTPUT "${SHADER_OUT_DIR}/cube.vert.spv"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}"
    COMMAND $<TARGET_FILE:Vulkan::glslc> -fshader-stage=vert "${SHADER_SRC_DIR}/cube.vert" -o "${SHADER_OUT_DIR}/cube.vert.spv"
    DEPENDS "${SHADER_SRC_DIR}/cube.vert"
    COMMENT "Compilando shader cube.vert"
    VERBATIM
)

add_custom_command(
    OUTPUT "${SHADER_OUT_DIR}/cube.frag.spv"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}"
    COMMAND $<TARGET_FILE:Vulkan::glslc> -fshader-stage=frag "${SHADER_SRC_DIR}/cube.frag" -o "${SHADER_OUT_DIR}/cube.frag.spv"
    DEPENDS "${SHADER_SRC_DIR}/cube.frag"
    COMMENT "Compilando shader cube.frag"
    VERBATIM
)

add_custom_target(argb-shaders
    DEPENDS
        "${SHADER_OUT_DIR}/cube.vert.spv"
        "${SHADER_OUT_DIR}/cube.frag.spv"
)

# ---------------------------------------------------------------------------
# argb-render (Fase 02)
# ---------------------------------------------------------------------------
add_executable(argb-render
    src/renderer_main.cpp
    src/renderer/vulkan_renderer.cpp
    src/renderer/camera.cpp
    src/renderer/mesh.cpp
    src/core/vk_util.cpp
    src/gpu/gpu_context.cpp
    src/gpu/gpu_selector.cpp
)

add_dependencies(argb-render argb-shaders)

target_include_directories(argb-render PRIVATE src)
target_link_libraries(argb-render PRIVATE Vulkan::Vulkan)

if(MSVC)
    target_compile_definitions(argb-render PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(argb-render PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(argb-render PRIVATE -Wall -Wextra -Wpedantic)
endif()
