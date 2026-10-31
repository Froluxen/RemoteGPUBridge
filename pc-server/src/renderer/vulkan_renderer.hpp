#pragma once

#include "gpu/gpu_context.hpp"
#include "gpu/gpu_types.hpp"
#include "renderer/camera.hpp"
#include "renderer/math.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

#include <vulkan/vulkan.h>

namespace argb::renderer {

struct RendererConfig {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::filesystem::path shaderDir;
};

struct FrameStats {
    double cpuFrameMs = 0.0;
};

class VulkanRenderer {
public:
    VulkanRenderer() = default;
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    bool initialize(gpu::GpuContext& context,
                    const gpu::PhysicalDeviceInfo& physicalDevice,
                    std::uint32_t graphicsQueueFamily,
                    const RendererConfig& config);

    void shutdown();

    Camera& camera() { return camera_; }
    const Camera& camera() const { return camera_; }

    bool renderFrame(float timeSeconds, FrameStats& stats);
    bool saveSnapshot(const std::filesystem::path& ppmPath);

    std::uint32_t width() const { return config_.width; }
    std::uint32_t height() const { return config_.height; }

private:
    struct UniformBufferObject {
        math::Mat4 mvp;
    };

    bool createRenderPass();
    bool createDescriptorSetLayout();
    bool createPipeline();
    bool createColorResources();
    bool createDepthResources();
    bool createFramebuffer();
    bool createCommandResources();
    bool createVertexBuffer();
    bool createIndexBuffer();
    bool createUniformBuffer();
    bool createDescriptorPool();
    bool createDescriptorSet();
    bool createSyncObjects();
    bool createReadbackBuffer();

    bool createBuffer(VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer,
                      VkDeviceMemory& memory);
    bool createImage(std::uint32_t width,
                     std::uint32_t height,
                     VkFormat format,
                     VkImageTiling tiling,
                     VkImageUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     VkImage& image,
                     VkDeviceMemory& memory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect) const;
    std::uint32_t findMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates,
                                 VkImageTiling tiling,
                                 VkFormatFeatureFlags features) const;
    VkShaderModule createShaderModule(const std::vector<char>& code) const;
    bool copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    void destroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory);
    void destroyImage(VkImage& image, VkDeviceMemory& memory, VkImageView& view);

    gpu::GpuContext* context_ = nullptr;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    RendererConfig config_;
    Camera camera_;

    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;

    VkImage colorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory_ = VK_NULL_HANDLE;
    VkImageView colorView_ = VK_NULL_HANDLE;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory_ = VK_NULL_HANDLE;
    std::uint32_t indexCount_ = 0;

    VkBuffer uniformBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory_ = VK_NULL_HANDLE;
    void* uniformMapped_ = nullptr;

    VkBuffer readbackBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory readbackMemory_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
};

} // namespace argb::renderer
