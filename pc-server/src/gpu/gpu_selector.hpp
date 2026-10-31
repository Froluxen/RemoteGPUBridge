#pragma once

#include "gpu/gpu_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace argb::gpu {

struct SelectionCriteria {
    std::string nameContains = "RTX 4060";
    uint32_t preferredVendorId = 0x10DEu; // NVIDIA
    bool requireDiscrete = false;
};

std::vector<PhysicalDeviceInfo> enumeratePhysicalDevices(VkInstance instance);

std::optional<std::size_t> selectPhysicalDevice(const std::vector<PhysicalDeviceInfo>& devices,
                                                const SelectionCriteria& criteria);

std::optional<uint32_t> findGraphicsQueueFamily(const PhysicalDeviceInfo& device);

} // namespace argb::gpu
