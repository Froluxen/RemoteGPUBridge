#include "gpu/gpu_context.hpp"

#include "core/log.hpp"
#include "core/vk_util.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <vector>

namespace argb::gpu {
namespace {

constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

bool hasInstanceExtension(const char* name) {
    uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) return false;
    std::vector<VkExtensionProperties> properties(count);
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, properties.data()) != VK_SUCCESS) {
        return false;
    }
    for (const auto& property : properties) {
        if (std::strcmp(property.extensionName, name) == 0) return true;
    }
    return false;
}

bool hasValidationLayer() {
    uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) return false;
    std::vector<VkLayerProperties> properties(count);
    if (vkEnumerateInstanceLayerProperties(&count, properties.data()) != VK_SUCCESS) return false;
    for (const auto& property : properties) {
        if (std::strcmp(property.layerName, kValidationLayerName) == 0) return true;
    }
    return false;
}

VkResult createDebugUtilsMessenger(VkInstance instance,
                                   const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
                                   VkDebugUtilsMessengerEXT* messenger) {
    const auto function = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (function == nullptr) return VK_ERROR_EXTENSION_NOT_PRESENT;
    return function(instance, createInfo, nullptr, messenger);
}

void destroyDebugUtilsMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger) {
    const auto function = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (function != nullptr) function(instance, messenger, nullptr);
}

} // namespace

GpuContext::~GpuContext() {
    shutdown();
}

bool GpuContext::createInstance(bool requestValidation) {
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&loaderVersion) != VK_SUCCESS) {
        loaderVersion = VK_API_VERSION_1_0;
    }
    log::info(std::format("Vulkan loader version: {}", vk::versionString(loaderVersion)));

    std::vector<const char*> extensions;
    const bool debugUtilsAvailable = hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (requestValidation) {
        if (!hasValidationLayer()) {
            log::warn("La capa VK_LAYER_KHRONOS_validation no esta disponible; se continua sin validacion.");
        } else if (!debugUtilsAvailable) {
            log::warn("VK_EXT_debug_utils no esta disponible; no se recibiran mensajes de validacion.");
        } else {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            validationEnabled_ = true;
        }
    }

    VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    applicationInfo.pApplicationName = "ARGB gpu-info";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.pEngineName = "ARGB";
    applicationInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.apiVersion = std::min(loaderVersion, VK_API_VERSION_1_3);

    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

    if (validationEnabled_) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &kValidationLayerName;
    }

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    if (validationEnabled_) {
        debugCreateInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = &GpuContext::debugCallback;
        createInfo.pNext = &debugCreateInfo;
    }

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkCreateInstance fallo: {}", vk::resultString(result)));
        instance_ = VK_NULL_HANDLE;
        return false;
    }

    if (validationEnabled_ && !setupDebugMessenger()) {
        log::warn("No se pudo crear el debug messenger; se continua sin mensajes de validacion.");
    }

    log::info("Instancia Vulkan creada.");
    return true;
}

bool GpuContext::setupDebugMessenger() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = &GpuContext::debugCallback;

    const VkResult result = createDebugUtilsMessenger(instance_, &createInfo, &debugMessenger_);
    if (result != VK_SUCCESS) {
        log::warn(std::format("vkCreateDebugUtilsMessengerEXT fallo: {}", vk::resultString(result)));
        debugMessenger_ = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool GpuContext::createLogicalDevice(const PhysicalDeviceInfo& physicalDevice,
                                     uint32_t graphicsQueueFamilyIndex) {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledExtensionCount = 0;
    createInfo.ppEnabledExtensionNames = nullptr;
    createInfo.pEnabledFeatures = nullptr;

    const VkResult result = vkCreateDevice(physicalDevice.handle, &createInfo, nullptr, &device_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkCreateDevice fallo: {}", vk::resultString(result)));
        device_ = VK_NULL_HANDLE;
        return false;
    }

    vkGetDeviceQueue(device_, graphicsQueueFamilyIndex, 0, &graphicsQueue_);
    log::info(std::format("Logical device creado (queue family {}, graphics queue obtenida).",
                          graphicsQueueFamilyIndex));
    return true;
}

void GpuContext::shutdown() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
        graphicsQueue_ = VK_NULL_HANDLE;
        log::info("Logical device destruido.");
    }
    if (debugMessenger_ != VK_NULL_HANDLE) {
        destroyDebugUtilsMessenger(instance_, debugMessenger_);
        debugMessenger_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
        log::info("Instancia Vulkan destruida.");
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL GpuContext::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*types*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userData*/) {
    const char* message = (data != nullptr && data->pMessage != nullptr) ? data->pMessage : "(sin mensaje)";
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        log::error(std::format("Vulkan: {}", message));
    } else {
        log::warn(std::format("Vulkan: {}", message));
    }
    return VK_FALSE;
}

} // namespace argb::gpu
