#pragma once

#include "gpu/gpu_types.hpp"

#include <vulkan/vulkan.h>

namespace argb::gpu {

class GpuContext {
public:
    GpuContext() = default;
    ~GpuContext();

    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;
    GpuContext(GpuContext&&) = delete;
    GpuContext& operator=(GpuContext&&) = delete;

    bool createInstance(bool requestValidation);
    bool createLogicalDevice(const PhysicalDeviceInfo& physicalDevice,
                             uint32_t graphicsQueueFamilyIndex);

    void shutdown();

    VkInstance instance() const { return instance_; }
    VkDevice device() const { return device_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    bool validationEnabled() const { return validationEnabled_; }

private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* userData);

    bool setupDebugMessenger();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    bool validationEnabled_ = false;
};

} // namespace argb::gpu
