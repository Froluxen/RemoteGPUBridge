#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace argb::gpu {

struct QueueFamilyInfo {
    uint32_t index = 0;
    VkQueueFamilyProperties properties{};
};

struct PhysicalDeviceInfo {
    VkPhysicalDevice handle = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceFeatures features{};
    std::vector<QueueFamilyInfo> queueFamilies;
    std::vector<std::string> extensions;
    bool hasGraphicsQueue = false;
    bool isDiscrete = false;
};

} // namespace argb::gpu
