find_package(Vulkan REQUIRED)

add_executable(gpu-info
    src/main.cpp
    src/core/vk_util.cpp
    src/gpu/gpu_context.cpp
    src/gpu/gpu_selector.cpp
)

target_include_directories(gpu-info PRIVATE src)

if(MSVC)
    target_compile_options(gpu-info PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(gpu-info PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(gpu-info PRIVATE Vulkan::Vulkan)
