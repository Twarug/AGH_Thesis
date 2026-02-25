#include "TemporalDependencyCase.h"
#include "../RendererData.h"
#include <print>
#include <array>

void TemporalDependencyScene::createPingPongResources(VulkanBaseRenderer* renderer, size_t gpuIndex)
{
    VkDevice device = renderer->getDevice(gpuIndex);
    auto& res = gpuResources[gpuIndex];

    auto [width, height] = renderer->getRenderDimensions(gpuIndex);

        for (int i = 0; i < 2; i++)
    {
        renderer->createImage(gpuIndex, width, height, VK_FORMAT_B8G8R8A8_UNORM,
                              VK_IMAGE_TILING_OPTIMAL,
                              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              res.images[i], res.imageMemories[i]);

                res.imageViews[i] = renderer->createImageView(device, res.images[i],
                                                       VK_FORMAT_B8G8R8A8_UNORM,
                                                       VK_IMAGE_ASPECT_COLOR_BIT);
    }

        VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &res.sampler) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create temporal sampler!");
    }

        VkCommandBuffer cmdBuffer = renderer->beginSingleTimeCommands(gpuIndex);
    for (int i = 0; i < 2; i++)
    {
        renderer->transitionImageLayout(cmdBuffer, res.images[i],
                                         VK_IMAGE_LAYOUT_UNDEFINED,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    renderer->endSingleTimeCommands(gpuIndex, cmdBuffer);

    std::println("Created temporal ping-pong resources for GPU {} ({}x{})", gpuIndex, width, height);
}

void TemporalDependencyScene::createTemporalRenderPass(VulkanBaseRenderer* renderer, size_t gpuIndex) {}


void TemporalDependencyScene::createBuffers(VulkanBaseRenderer* renderer)
{
    rendererRef = renderer;
    size_t deviceCount = renderer->getDeviceCount();

    gpuResources.resize(deviceCount);
    temporalRenderPasses.resize(deviceCount, VK_NULL_HANDLE);

    for (size_t i = 0; i < deviceCount; i++)
    {
        createPingPongResources(renderer, i);
    }
}

void TemporalDependencyScene::destroyBuffers(VulkanBaseRenderer* renderer)
{
    for (size_t i = 0; i < gpuResources.size(); i++)
    {
        VkDevice device = renderer->getDevice(i);
        auto& res = gpuResources[i];

        if (res.sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(device, res.sampler, nullptr);
        }

        for (int j = 0; j < 2; j++)
        {
            if (res.imageViews[j] != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device, res.imageViews[j], nullptr);
            }
            if (res.images[j] != VK_NULL_HANDLE)
            {
                vkDestroyImage(device, res.images[j], nullptr);
            }
            if (res.imageMemories[j] != VK_NULL_HANDLE)
            {
                vkFreeMemory(device, res.imageMemories[j], nullptr);
            }
        }
    }

    gpuResources.clear();
    temporalRenderPasses.clear();
    rendererRef = nullptr;
}

void TemporalDependencyScene::createDescriptorSetLayout(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                                        VkDescriptorSetLayout& layout)
{
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;

    if (vkCreateDescriptorSetLayout(renderer->getDevice(gpuIndex), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create temporal descriptor set layout!");
    }
}

void TemporalDependencyScene::createDescriptorPool(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                                   size_t imageCount, VkDescriptorPool& pool)
{
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = static_cast<uint32_t>(imageCount + 2);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = static_cast<uint32_t>(imageCount + 2);

    if (vkCreateDescriptorPool(renderer->getDevice(gpuIndex), &poolInfo, nullptr, &pool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create temporal descriptor pool!");
    }
}

void TemporalDependencyScene::createDescriptorSets(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                                   size_t imageCount, VkDescriptorSetLayout layout,
                                                   VkDescriptorPool pool,
                                                   std::vector<VkBuffer>& uniformBuffers,
                                                   std::vector<VkDescriptorSet>& descriptorSets)
{
    VkDevice device = renderer->getDevice(gpuIndex);
    auto& res = gpuResources[gpuIndex];

    std::array layouts = {layout, layout};

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &allocInfo, res.descriptorSets.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate temporal descriptor sets!");
    }

    for (int i = 0; i < 2; i++)
    {
        int sourceImage = 1 - i;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = res.imageViews[sourceImage];
        imageInfo.sampler = res.sampler;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = res.descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }

    descriptorSets.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++)
    {
        descriptorSets[i] = res.descriptorSets[res.currentIndex];
    }
}

void TemporalDependencyScene::createUniformBuffers(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                                   size_t imageCount,
                                                   std::vector<VkBuffer>& buffers,
                                                   std::vector<VkDeviceMemory>& memories)
{
    buffers.clear();
    memories.clear();
}

void TemporalDependencyScene::updateUniformBuffer(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                                  uint32_t currentImage, VkDeviceMemory memory)
{
    gpuResources[gpuIndex].currentIndex = 1 - gpuResources[gpuIndex].currentIndex;
}

VkFramebuffer TemporalDependencyScene::getCurrentFramebuffer(size_t gpuIndex) const
{
    return gpuResources[gpuIndex].framebuffers[gpuResources[gpuIndex].currentIndex];
}

VkDescriptorSet TemporalDependencyScene::getCurrentDescriptorSet(size_t gpuIndex) const
{
    return gpuResources[gpuIndex].descriptorSets[gpuResources[gpuIndex].currentIndex];
}

void TemporalDependencyScene::preFrameUpdate(VulkanBaseRenderer* renderer, size_t gpuIndex) {}

void TemporalDependencyScene::postFrameUpdate(VulkanBaseRenderer* renderer, size_t gpuIndex) {}

void TemporalDependencyScene::recordDrawCommands(VkCommandBuffer commandBuffer, size_t gpuIndex)
{
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}

REGISTER_BENCHMARK_CASE(TemporalDependencyCase)
