#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace argb::vk {

std::string resultString(VkResult result);
std::string versionString(uint32_t version);
std::string driverVersionString(uint32_t vendorId, uint32_t driverVersion);
std::string deviceTypeString(VkPhysicalDeviceType type);
std::string queueFlagsString(VkQueueFlags flags);
bool isNvidiaVendor(uint32_t vendorId);

} // namespace argb::vk
