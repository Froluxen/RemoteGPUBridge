#include "gpu/gpu_selector.hpp"

#include "core/log.hpp"
#include "core/vk_util.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <string_view>

namespace argb::gpu {
namespace {

std::string toLower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool containsCaseInsensitive(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

} // namespace

std::vector<PhysicalDeviceInfo> enumeratePhysicalDevices(VkInstance instance) {
    uint32_t deviceCount = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (result != VK_SUCCESS || deviceCount == 0) {
        if (result != VK_SUCCESS) {
            log::error(std::format("vkEnumeratePhysicalDevices fallo: {}", vk::resultString(result)));
        }
        return {};
    }

    std::vector<VkPhysicalDevice> handles(deviceCount);
    result = vkEnumeratePhysicalDevices(instance, &deviceCount, handles.data());
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
        log::error(std::format("vkEnumeratePhysicalDevices fallo: {}", vk::resultString(result)));
        return {};
    }
    handles.resize(deviceCount);

    std::vector<PhysicalDeviceInfo> devices;
    devices.reserve(handles.size());

    for (VkPhysicalDevice handle : handles) {
        PhysicalDeviceInfo info;
        info.handle = handle;
        vkGetPhysicalDeviceProperties(handle, &info.properties);
        vkGetPhysicalDeviceFeatures(handle, &info.features);
        info.isDiscrete = info.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(handle, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(handle, &queueFamilyCount, queueFamilyProperties.data());
        info.queueFamilies.reserve(queueFamilyCount);
        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            QueueFamilyInfo family;
            family.index = i;
            family.properties = queueFamilyProperties[i];
            info.queueFamilies.push_back(family);
            if (queueFamilyProperties[i].queueCount > 0 &&
                (queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                info.hasGraphicsQueue = true;
            }
        }

        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(handle, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensionProperties(extensionCount);
        vkEnumerateDeviceExtensionProperties(handle, nullptr, &extensionCount, extensionProperties.data());
        info.extensions.reserve(extensionCount);
        for (const auto& extension : extensionProperties) {
            info.extensions.emplace_back(extension.extensionName);
        }

        devices.push_back(std::move(info));
    }

    return devices;
}

std::optional<std::size_t> selectPhysicalDevice(const std::vector<PhysicalDeviceInfo>& devices,
                                                const SelectionCriteria& criteria) {
    const auto matches = [&criteria](const PhysicalDeviceInfo& device) {
        if (!device.hasGraphicsQueue) return false;
        if (criteria.requireDiscrete && !device.isDiscrete) return false;
        if (criteria.nameContains.empty()) {
            return device.properties.vendorID == criteria.preferredVendorId && device.isDiscrete;
        }
        return containsCaseInsensitive(device.properties.deviceName, criteria.nameContains);
    };

    // Primero: coincidencia de nombre + vendor preferido (independiente del orden).
    for (std::size_t i = 0; i < devices.size(); ++i) {
        if (matches(devices[i]) && devices[i].properties.vendorID == criteria.preferredVendorId) {
            return i;
        }
    }
    // Segundo: coincidencia de nombre con cualquier vendor.
    for (std::size_t i = 0; i < devices.size(); ++i) {
        if (matches(devices[i])) return i;
    }
    return std::nullopt;
}

std::optional<uint32_t> findGraphicsQueueFamily(const PhysicalDeviceInfo& device) {
    for (const auto& family : device.queueFamilies) {
        if (family.properties.queueCount > 0 &&
            (family.properties.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            return family.index;
        }
    }
    return std::nullopt;
}

} // namespace argb::gpu
