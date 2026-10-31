#include "core/vk_util.hpp"

#include <format>

namespace argb::vk {

std::string resultString(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        default: return std::format("VkResult({})", static_cast<int>(result));
    }
}

std::string versionString(uint32_t version) {
    return std::format("{}.{}.{}",
                       VK_VERSION_MAJOR(version),
                       VK_VERSION_MINOR(version),
                       VK_VERSION_PATCH(version));
}

namespace {

// NVIDIA empaqueta su driverVersion como (major << 22) | (minor << 14) |
// (patch << 6) | subpatch, distinto del formato generico de VK_VERSION_*.
std::string nvidiaDriverVersionString(uint32_t driverVersion) {
    const uint32_t major = (driverVersion >> 22) & 0x3FF;
    const uint32_t minor = (driverVersion >> 14) & 0xFF;
    const uint32_t patch = (driverVersion >> 6) & 0xFF;
    const uint32_t subpatch = driverVersion & 0x3F;
    return std::format("{}.{}.{}.{}", major, minor, patch, subpatch);
}

} // namespace

std::string driverVersionString(uint32_t vendorId, uint32_t driverVersion) {
    if (isNvidiaVendor(vendorId)) return nvidiaDriverVersionString(driverVersion);
    return versionString(driverVersion);
}

std::string deviceTypeString(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER: return "OTHER";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "INTEGRATED_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "DISCRETE_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "VIRTUAL_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
        default: return std::format("UNKNOWN({})", static_cast<int>(type));
    }
}

std::string queueFlagsString(VkQueueFlags flags) {
    std::string out;
    const auto add = [&out](const char* name) {
        if (!out.empty()) out += '|';
        out += name;
    };
    if ((flags & VK_QUEUE_GRAPHICS_BIT) != 0) add("GRAPHICS");
    if ((flags & VK_QUEUE_COMPUTE_BIT) != 0) add("COMPUTE");
    if ((flags & VK_QUEUE_TRANSFER_BIT) != 0) add("TRANSFER");
    if ((flags & VK_QUEUE_SPARSE_BINDING_BIT) != 0) add("SPARSE_BINDING");
    if ((flags & VK_QUEUE_PROTECTED_BIT) != 0) add("PROTECTED");
    if (out.empty()) out = "NONE";
    return out;
}

bool isNvidiaVendor(uint32_t vendorId) {
    return vendorId == 0x10DEu;
}

} // namespace argb::vk
