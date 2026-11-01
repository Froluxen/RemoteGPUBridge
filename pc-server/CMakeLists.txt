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
# Tests (sin GPU)
# ---------------------------------------------------------------------------
add_executable(input-processor-test
    ${CMAKE_SOURCE_DIR}/tests/input_processor_test.cpp
    src/input/input_processor.cpp
)
target_include_directories(input-processor-test PRIVATE src)

if(MSVC)
    target_compile_options(input-processor-test PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(input-processor-test PRIVATE -Wall -Wextra -Wpedantic)
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

# ---------------------------------------------------------------------------
# argb-encode (Fase 03) - requiere FFmpeg con h264_nvenc
# ---------------------------------------------------------------------------
if(NOT FFMPEG_ROOT AND DEFINED ENV{FFMPEG_ROOT})
    set(FFMPEG_ROOT "$ENV{FFMPEG_ROOT}")
endif()

if(FFMPEG_ROOT AND EXISTS "${FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    find_library(AVCODEC_LIBRARY NAMES avcodec HINTS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
    find_library(AVUTIL_LIBRARY NAMES avutil HINTS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)

    if(AVCODEC_LIBRARY AND AVUTIL_LIBRARY)
        add_executable(argb-encode
            src/encode_main.cpp
            src/encoders/ffmpeg_nvenc_encoder.cpp
            src/encoders/video_packetizer.cpp
            src/renderer/vulkan_renderer.cpp
            src/renderer/camera.cpp
            src/renderer/mesh.cpp
            src/core/vk_util.cpp
            src/gpu/gpu_context.cpp
            src/gpu/gpu_selector.cpp
        )

        add_dependencies(argb-encode argb-shaders)
        target_include_directories(argb-encode PRIVATE src "${FFMPEG_ROOT}/include")
        target_link_libraries(argb-encode PRIVATE Vulkan::Vulkan "${AVCODEC_LIBRARY}" "${AVUTIL_LIBRARY}")

        if(MSVC)
            target_compile_definitions(argb-encode PRIVATE _CRT_SECURE_NO_WARNINGS)
            target_compile_options(argb-encode PRIVATE /W4 /permissive- /utf-8)
        else()
            target_compile_options(argb-encode PRIVATE -Wall -Wextra -Wpedantic)
        endif()

        # -------------------------------------------------------------------
        # argb-server (Fase 04): render + encode + transporte TCP
        # -------------------------------------------------------------------
        add_executable(argb-server
            src/transport_main.cpp
            src/transport/tcp_server.cpp
            src/input/input_processor.cpp
            src/encoders/ffmpeg_nvenc_encoder.cpp
            src/renderer/vulkan_renderer.cpp
            src/renderer/camera.cpp
            src/renderer/mesh.cpp
            src/core/vk_util.cpp
            src/gpu/gpu_context.cpp
            src/gpu/gpu_selector.cpp
        )

        add_dependencies(argb-server argb-shaders)
        target_include_directories(argb-server PRIVATE src "${CMAKE_SOURCE_DIR}/shared" "${FFMPEG_ROOT}/include")
        target_link_libraries(argb-server PRIVATE Vulkan::Vulkan "${AVCODEC_LIBRARY}" "${AVUTIL_LIBRARY}" ws2_32 winmm)

        if(MSVC)
            target_compile_definitions(argb-server PRIVATE _CRT_SECURE_NO_WARNINGS)
            target_compile_options(argb-server PRIVATE /W4 /permissive- /utf-8)
        else()
            target_compile_options(argb-server PRIVATE -Wall -Wextra -Wpedantic)
        endif()

        file(GLOB FFMPEG_DLLS "${FFMPEG_ROOT}/bin/*.dll")
        if(FFMPEG_DLLS)
            add_custom_command(TARGET argb-encode POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_DLLS} "$<TARGET_FILE_DIR:argb-encode>"
                COMMENT "Copiando DLLs de FFmpeg junto a argb-encode"
            )
            add_custom_command(TARGET argb-server POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_DLLS} "$<TARGET_FILE_DIR:argb-server>"
                COMMENT "Copiando DLLs de FFmpeg junto a argb-server"
            )
        endif()

        message(STATUS "FFmpeg (h264_nvenc) encontrado en ${FFMPEG_ROOT}")
    else()
        message(WARNING "FFmpeg headers encontrados pero faltan avcodec/avutil; argb-encode no se compilara.")
    endif()
else()
    message(WARNING "FFMPEG_ROOT no definido o invalido; argb-encode no se compilara.")
endif()
