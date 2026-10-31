#include "renderer/vulkan_renderer.hpp"

#include "core/log.hpp"
#include "core/vk_util.hpp"
#include "renderer/mesh.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <format>
#include <fstream>
#include <string>
#include <vector>

namespace argb::renderer {
namespace {

std::vector<char> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        log::error(std::format("No se pudo abrir '{}'.", path.string()));
        return {};
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        log::error(std::format("Archivo vacio: '{}'.", path.string()));
        return {};
    }
    std::vector<char> buffer(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(buffer.data(), size);
    return buffer;
}

bool writePpm(const std::filesystem::path& path,
              std::uint32_t width,
              std::uint32_t height,
              const std::vector<std::uint8_t>& pixels,
              VkFormat format) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        log::error(std::format("No se pudo crear '{}'.", path.string()));
        return false;
    }

    file << "P6\n" << width << ' ' << height << "\n255\n";

    const bool bgra = (format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB);
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const std::uint8_t r = bgra ? pixels[i + 2] : pixels[i];
        const std::uint8_t g = pixels[i + 1];
        const std::uint8_t b = bgra ? pixels[i] : pixels[i + 2];
        file.put(static_cast<char>(r));
        file.put(static_cast<char>(g));
        file.put(static_cast<char>(b));
    }

    return file.good();
}

} // namespace

VulkanRenderer::~VulkanRenderer() {
    shutdown();
}

bool VulkanRenderer::initialize(gpu::GpuContext& context,
                                const gpu::PhysicalDeviceInfo& physicalDevice,
                                std::uint32_t graphicsQueueFamily,
                                const RendererConfig& config) {
    context_ = &context;
    physicalDevice_ = physicalDevice.handle;
    device_ = context.device();
    queue_ = context.graphicsQueue();
    queueFamily_ = graphicsQueueFamily;
    config_ = config;
    config_.width = std::max<std::uint32_t>(1, config_.width);
    config_.height = std::max<std::uint32_t>(1, config_.height);

    colorFormat_ = findSupportedFormat(
        {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
         VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
    if (colorFormat_ == VK_FORMAT_UNDEFINED) {
        log::error("No se encontro un formato de color compatible.");
        return false;
    }

    depthFormat_ = findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    if (depthFormat_ == VK_FORMAT_UNDEFINED) {
        log::error("No se encontro un formato de profundidad compatible.");
        return false;
    }

    if (!createRenderPass()) return false;
    if (!createDescriptorSetLayout()) return false;
    if (!createPipeline()) return false;
    if (!createCommandResources()) return false;
    if (!createColorResources()) return false;
    if (!createDepthResources()) return false;
    if (!createFramebuffer()) return false;
    if (!createVertexBuffer()) return false;
    if (!createIndexBuffer()) return false;
    if (!createUniformBuffer()) return false;
    if (!createDescriptorPool()) return false;
    if (!createDescriptorSet()) return false;
    if (!createSyncObjects()) return false;
    if (!createReadbackBuffer()) return false;

    log::info(std::format("Renderer listo: {}x{} (color VkFormat {}, depth VkFormat {}).",
                          config_.width,
                          config_.height,
                          static_cast<int>(colorFormat_),
                          static_cast<int>(depthFormat_)));
    return true;
}

bool VulkanRenderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = colorFormat_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat_;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference{};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    const std::array<VkAttachmentDescription, 2> attachments{colorAttachment, depthAttachment};

    VkRenderPassCreateInfo createInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    createInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    const VkResult result = vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkCreateRenderPass fallo: {}", vk::resultString(result)));
        return false;
    }
    return true;
}

bool VulkanRenderer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo createInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    createInfo.bindingCount = 1;
    createInfo.pBindings = &binding;

    const VkResult result = vkCreateDescriptorSetLayout(device_, &createInfo, nullptr, &descriptorSetLayout_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkCreateDescriptorSetLayout fallo: {}", vk::resultString(result)));
        return false;
    }
    return true;
}

bool VulkanRenderer::createPipeline() {
    const std::vector<char> vertexCode = readBinaryFile(config_.shaderDir / "cube.vert.spv");
    const std::vector<char> fragmentCode = readBinaryFile(config_.shaderDir / "cube.frag.spv");
    if (vertexCode.empty() || fragmentCode.empty() || vertexCode.size() % 4 != 0 || fragmentCode.size() % 4 != 0) {
        log::error("No se pudieron cargar los shaders SPIR-V compilados.");
        return false;
    }

    const VkShaderModule vertexModule = createShaderModule(vertexCode);
    const VkShaderModule fragmentModule = createShaderModule(fragmentCode);
    if (vertexModule == VK_NULL_HANDLE || fragmentModule == VK_NULL_HANDLE) {
        log::error("No se pudieron crear los shader modules.");
        if (vertexModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, vertexModule, nullptr);
        if (fragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, fragmentModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo vertexStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = vertexModule;
    vertexStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = fragmentModule;
    fragmentStage.pName = "main";

    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{vertexStage, fragmentStage};

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, position);
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDescription;
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributeDescriptions.size());
    vertexInput.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(config_.width);
    viewport.height = static_cast<float>(config_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {config_.width, config_.height};

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                          VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT |
                                          VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;

    VkResult result = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkCreatePipelineLayout fallo: {}", vk::resultString(result)));
        vkDestroyShaderModule(device_, vertexModule, nullptr);
        vkDestroyShaderModule(device_, fragmentModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;

    result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);

    vkDestroyShaderModule(device_, vertexModule, nullptr);
    vkDestroyShaderModule(device_, fragmentModule, nullptr);

    if (result != VK_SUCCESS) {
        log::error(std::format("vkCreateGraphicsPipelines fallo: {}", vk::resultString(result)));
        return false;
    }
    return true;
}

bool VulkanRenderer::createColorResources() {
    if (!createImage(config_.width,
                     config_.height,
                     colorFormat_,
                     VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     colorImage_,
                     colorMemory_)) {
        return false;
    }
    colorView_ = createImageView(colorImage_, colorFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
    return colorView_ != VK_NULL_HANDLE;
}

bool VulkanRenderer::createDepthResources() {
    if (!createImage(config_.width,
                     config_.height,
                     depthFormat_,
                     VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     depthImage_,
                     depthMemory_)) {
        return false;
    }
    depthView_ = createImageView(depthImage_, depthFormat_, VK_IMAGE_ASPECT_DEPTH_BIT);
    return depthView_ != VK_NULL_HANDLE;
}

bool VulkanRenderer::createFramebuffer() {
    const std::array<VkImageView, 2> attachments{colorView_, depthView_};

    VkFramebufferCreateInfo createInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    createInfo.renderPass = renderPass_;
    createInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.width = config_.width;
    createInfo.height = config_.height;
    createInfo.layers = 1;

    const VkResult result = vkCreateFramebuffer(device_, &createInfo, nullptr, &framebuffer_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkCreateFramebuffer fallo: {}", vk::resultString(result)));
        return false;
    }
    return true;
}

bool VulkanRenderer::createCommandResources() {
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily_;

    VkResult result = vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkCreateCommandPool fallo: {}", vk::resultString(result)));
        return false;
    }

    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = commandPool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;

    result = vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkAllocateCommandBuffers fallo: {}", vk::resultString(result)));
        return false;
    }
    return true;
}

bool VulkanRenderer::createVertexBuffer() {
    const MeshData mesh = makeCubeMesh();
    const VkDeviceSize bufferSize = sizeof(Vertex) * mesh.vertices.size();

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    if (!createBuffer(bufferSize,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      stagingBuffer,
                      stagingMemory)) {
        return false;
    }

    void* data = nullptr;
    vkMapMemory(device_, stagingMemory, 0, bufferSize, 0, &data);
    std::memcpy(data, mesh.vertices.data(), static_cast<std::size_t>(bufferSize));
    vkUnmapMemory(device_, stagingMemory);

    const bool created = createBuffer(bufferSize,
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                      vertexBuffer_,
                                      vertexMemory_);
    if (created) {
        copyBuffer(stagingBuffer, vertexBuffer_, bufferSize);
    }

    destroyBuffer(stagingBuffer, stagingMemory);
    return created;
}

bool VulkanRenderer::createIndexBuffer() {
    const MeshData mesh = makeCubeMesh();
    indexCount_ = static_cast<std::uint32_t>(mesh.indices.size());
    const VkDeviceSize bufferSize = sizeof(std::uint16_t) * mesh.indices.size();

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    if (!createBuffer(bufferSize,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      stagingBuffer,
                      stagingMemory)) {
        return false;
    }

    void* data = nullptr;
    vkMapMemory(device_, stagingMemory, 0, bufferSize, 0, &data);
    std::memcpy(data, mesh.indices.data(), static_cast<std::size_t>(bufferSize));
    vkUnmapMemory(device_, stagingMemory);

    const bool created = createBuffer(bufferSize,
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                      indexBuffer_,
                                      indexMemory_);
    if (created) {
        copyBuffer(stagingBuffer, indexBuffer_, bufferSize);
    }

    destroyBuffer(stagingBuffer, stagingMemory);
    return created;
}

bool VulkanRenderer::createUniformBuffer() {
    const VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    if (!createBuffer(bufferSize,
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      uniformBuffer_,
                      uniformMemory_)) {
        return false;
    }
    const VkResult result = vkMapMemory(device_, uniformMemory_, 0, bufferSize, 0, &uniformMapped_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkMapMemory (uniform) fallo: {}", vk::resultString(result)));
        return false;
    }
    return true;
}

bool VulkanRenderer::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo createInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    createInfo.poolSizeCount = 1;
    createInfo.pPoolSizes = &poolSize;
    createInfo.maxSets = 1;

    const VkResult result = vkCreateDescriptorPool(device_, &createInfo, nullptr, &descriptorPool_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkCreateDescriptorPool fallo: {}", vk::resultString(result)));
        return false;
    }
    return true;
}

bool VulkanRenderer::createDescriptorSet() {
    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = descriptorPool_;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &descriptorSetLayout_;

    VkResult result = vkAllocateDescriptorSets(device_, &allocateInfo, &descriptorSet_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkAllocateDescriptorSets fallo: {}", vk::resultString(result)));
        return false;
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer_;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return true;
}

bool VulkanRenderer::createSyncObjects() {
    VkFenceCreateInfo createInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    const VkResult result = vkCreateFence(device_, &createInfo, nullptr, &fence_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkCreateFence fallo: {}", vk::resultString(result)));
        return false;
    }
    return true;
}

bool VulkanRenderer::createReadbackBuffer() {
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(config_.width) * config_.height * 4;
    return createBuffer(bufferSize,
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        readbackBuffer_,
                        readbackMemory_);
}

bool VulkanRenderer::renderFrame(float timeSeconds, FrameStats& stats) {
    const auto start = std::chrono::steady_clock::now();

    const math::Mat4 model = math::multiply(math::rotationY(timeSeconds * 0.9f),
                                            math::rotationX(timeSeconds * 0.6f));
    const math::Mat4 view = camera_.viewMatrix();
    const float aspect = static_cast<float>(config_.width) / static_cast<float>(config_.height);
    const math::Mat4 projection = camera_.projectionMatrix(aspect);

    UniformBufferObject ubo{};
    ubo.mvp = math::multiply(math::multiply(projection, view), model);
    std::memcpy(uniformMapped_, &ubo, sizeof(ubo));

    VkResult result = vkResetCommandBuffer(commandBuffer_, 0);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkResetCommandBuffer fallo: {}", vk::resultString(result)));
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    result = vkBeginCommandBuffer(commandBuffer_, &beginInfo);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkBeginCommandBuffer fallo: {}", vk::resultString(result)));
        return false;
    }

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color.float32[0] = 0.05f;
    clearValues[0].color.float32[1] = 0.05f;
    clearValues[0].color.float32[2] = 0.08f;
    clearValues[0].color.float32[3] = 1.0f;
    clearValues[1].depthStencil.depth = 1.0f;
    clearValues[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = framebuffer_;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {config_.width, config_.height};
    renderPassInfo.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer_, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                            0, 1, &descriptorSet_, 0, nullptr);

    const VkBuffer vertexBuffers[] = {vertexBuffer_};
    const VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer_, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer_, indexBuffer_, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(commandBuffer_, indexCount_, 1, 0, 0, 0);
    vkCmdEndRenderPass(commandBuffer_);

    result = vkEndCommandBuffer(commandBuffer_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkEndCommandBuffer fallo: {}", vk::resultString(result)));
        return false;
    }

    vkResetFences(device_, 1, &fence_);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer_;

    result = vkQueueSubmit(queue_, 1, &submitInfo, fence_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkQueueSubmit fallo: {}", vk::resultString(result)));
        return false;
    }

    result = vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkWaitForFences fallo: {}", vk::resultString(result)));
        return false;
    }

    const auto end = std::chrono::steady_clock::now();
    stats.cpuFrameMs = std::chrono::duration<double, std::milli>(end - start).count();
    return true;
}

bool VulkanRenderer::captureFrame(std::vector<std::uint8_t>& pixels) {
    VkResult result = vkResetCommandBuffer(commandBuffer_, 0);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkResetCommandBuffer fallo: {}", vk::resultString(result)));
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(commandBuffer_, &beginInfo);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkBeginCommandBuffer fallo: {}", vk::resultString(result)));
        return false;
    }

    VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = colorImage_;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer_,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {config_.width, config_.height, 1};

    vkCmdCopyImageToBuffer(commandBuffer_,
                           colorImage_,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackBuffer_,
                           1,
                           &region);

    VkImageMemoryBarrier toColor = toTransfer;
    toColor.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColor.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer_,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toColor);

    result = vkEndCommandBuffer(commandBuffer_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkEndCommandBuffer fallo: {}", vk::resultString(result)));
        return false;
    }

    vkResetFences(device_, 1, &fence_);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer_;

    result = vkQueueSubmit(queue_, 1, &submitInfo, fence_);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkQueueSubmit (snapshot) fallo: {}", vk::resultString(result)));
        return false;
    }
    result = vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkWaitForFences (snapshot) fallo: {}", vk::resultString(result)));
        return false;
    }

    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(config_.width) * config_.height * 4;
    void* mapped = nullptr;
    result = vkMapMemory(device_, readbackMemory_, 0, bufferSize, 0, &mapped);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkMapMemory (readback) fallo: {}", vk::resultString(result)));
        return false;
    }

    pixels.resize(static_cast<std::size_t>(bufferSize));
    std::memcpy(pixels.data(), mapped, static_cast<std::size_t>(bufferSize));
    vkUnmapMemory(device_, readbackMemory_);
    return true;
}

bool VulkanRenderer::saveSnapshot(const std::filesystem::path& ppmPath) {
    std::vector<std::uint8_t> pixels;
    if (!captureFrame(pixels)) {
        return false;
    }
    return writePpm(ppmPath, config_.width, config_.height, pixels, colorFormat_);
}

bool VulkanRenderer::createBuffer(VkDeviceSize size,
                                  VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags properties,
                                  VkBuffer& buffer,
                                  VkDeviceMemory& memory) {
    VkBufferCreateInfo createInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    createInfo.size = size;
    createInfo.usage = usage;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device_, &createInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkCreateBuffer fallo: {}", vk::resultString(result)));
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);

    const std::uint32_t memoryType = findMemoryType(requirements.memoryTypeBits, properties);
    if (memoryType == UINT32_MAX) {
        log::error("No se encontro un tipo de memoria adecuado para el buffer.");
        vkDestroyBuffer(device_, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;

    result = vkAllocateMemory(device_, &allocateInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkAllocateMemory fallo: {}", vk::resultString(result)));
        vkDestroyBuffer(device_, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(device_, buffer, memory, 0);
    return true;
}

bool VulkanRenderer::createImage(std::uint32_t width,
                                 std::uint32_t height,
                                 VkFormat format,
                                 VkImageTiling tiling,
                                 VkImageUsageFlags usage,
                                 VkMemoryPropertyFlags properties,
                                 VkImage& image,
                                 VkDeviceMemory& memory) {
    VkImageCreateInfo createInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    createInfo.imageType = VK_IMAGE_TYPE_2D;
    createInfo.extent = {width, height, 1};
    createInfo.mipLevels = 1;
    createInfo.arrayLayers = 1;
    createInfo.format = format;
    createInfo.tiling = tiling;
    createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    createInfo.usage = usage;
    createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateImage(device_, &createInfo, nullptr, &image);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkCreateImage fallo: {}", vk::resultString(result)));
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, image, &requirements);

    const std::uint32_t memoryType = findMemoryType(requirements.memoryTypeBits, properties);
    if (memoryType == UINT32_MAX) {
        log::error("No se encontro un tipo de memoria adecuado para la imagen.");
        vkDestroyImage(device_, image, nullptr);
        image = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;

    result = vkAllocateMemory(device_, &allocateInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkAllocateMemory (imagen) fallo: {}", vk::resultString(result)));
        vkDestroyImage(device_, image, nullptr);
        image = VK_NULL_HANDLE;
        return false;
    }

    vkBindImageMemory(device_, image, memory, 0);
    return true;
}

VkImageView VulkanRenderer::createImageView(VkImage image,
                                            VkFormat format,
                                            VkImageAspectFlags aspect) const {
    VkImageViewCreateInfo createInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    createInfo.image = image;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = format;
    createInfo.subresourceRange = {aspect, 0, 1, 0, 1};

    VkImageView view = VK_NULL_HANDLE;
    const VkResult result = vkCreateImageView(device_, &createInfo, nullptr, &view);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkCreateImageView fallo: {}", vk::resultString(result)));
        return VK_NULL_HANDLE;
    }
    return view;
}

std::uint32_t VulkanRenderer::findMemoryType(std::uint32_t typeFilter,
                                             VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
    for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) != 0 &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

VkFormat VulkanRenderer::findSupportedFormat(const std::vector<VkFormat>& candidates,
                                             VkImageTiling tiling,
                                             VkFormatFeatureFlags features) const {
    for (VkFormat format : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &properties);
        const VkFormatFeatureFlags supported =
            (tiling == VK_IMAGE_TILING_LINEAR) ? properties.linearTilingFeatures
                                               : properties.optimalTilingFeatures;
        if ((supported & features) == features) {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

VkShaderModule VulkanRenderer::createShaderModule(const std::vector<char>& code) const {
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const std::uint32_t*>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult result = vkCreateShaderModule(device_, &createInfo, nullptr, &module);
    if (result != VK_SUCCESS) {
        log::error(std::format("vkCreateShaderModule fallo: {}", vk::resultString(result)));
        return VK_NULL_HANDLE;
    }
    return module;
}

bool VulkanRenderer::copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    if (commandBuffer == VK_NULL_HANDLE) {
        return false;
    }

    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(commandBuffer, source, destination, 1, &region);
    endSingleTimeCommands(commandBuffer);
    return true;
}

VkCommandBuffer VulkanRenderer::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = commandPool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanRenderer::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
}

void VulkanRenderer::destroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory) {
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroyImage(VkImage& image, VkDeviceMemory& memory, VkImageView& view) {
    if (view != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE) {
        vkDestroyImage(device_, image, nullptr);
        image = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::shutdown() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle(device_);

    if (uniformMapped_ != nullptr) {
        vkUnmapMemory(device_, uniformMemory_);
        uniformMapped_ = nullptr;
    }

    destroyBuffer(uniformBuffer_, uniformMemory_);
    destroyBuffer(readbackBuffer_, readbackMemory_);
    destroyBuffer(indexBuffer_, indexMemory_);
    destroyBuffer(vertexBuffer_, vertexMemory_);

    if (fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(device_, fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
    }
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
        commandBuffer_ = VK_NULL_HANDLE;
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
        descriptorSet_ = VK_NULL_HANDLE;
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }

    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }

    destroyImage(colorImage_, colorMemory_, colorView_);
    destroyImage(depthImage_, depthMemory_, depthView_);

    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }

    device_ = VK_NULL_HANDLE;
    log::info("Renderer destruido.");
}

} // namespace argb::renderer
