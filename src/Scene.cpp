#include "Scene.h"
#include "VulkanBaseRenderer.h"

#include <chrono>
#include <cstring>

void Scene::createBuffers(VulkanBaseRenderer* renderer)
{
    if (vertices.empty() || indices.empty())
    {
        return;
    }

    size_t deviceCount = renderer->getDeviceCount();

    vertexBuffers.resize(deviceCount);
    vertexBufferMemories.resize(deviceCount);
    indexBuffers.resize(deviceCount);
    indexBufferMemories.resize(deviceCount);

    VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();
    VkDeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();

    for (size_t i = 0; i < deviceCount; i++)
    {
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;

        renderer->createBuffer(i, vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               stagingBuffer, stagingMemory);

        void* data;
        vkMapMemory(renderer->getDevice(i), stagingMemory, 0, vertexBufferSize, 0, &data);
        memcpy(data, vertices.data(), vertexBufferSize);
        vkUnmapMemory(renderer->getDevice(i), stagingMemory);

        renderer->createBuffer(i, vertexBufferSize,
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               vertexBuffers[i], vertexBufferMemories[i]);

        renderer->copyBuffer(i, stagingBuffer, vertexBuffers[i], vertexBufferSize);

        vkDestroyBuffer(renderer->getDevice(i), stagingBuffer, nullptr);
        vkFreeMemory(renderer->getDevice(i), stagingMemory, nullptr);

        renderer->createBuffer(i, indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               stagingBuffer, stagingMemory);

        vkMapMemory(renderer->getDevice(i), stagingMemory, 0, indexBufferSize, 0, &data);
        memcpy(data, indices.data(), indexBufferSize);
        vkUnmapMemory(renderer->getDevice(i), stagingMemory);

        renderer->createBuffer(i, indexBufferSize,
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               indexBuffers[i], indexBufferMemories[i]);

        renderer->copyBuffer(i, stagingBuffer, indexBuffers[i], indexBufferSize);

        vkDestroyBuffer(renderer->getDevice(i), stagingBuffer, nullptr);
        vkFreeMemory(renderer->getDevice(i), stagingMemory, nullptr);
    }
}

void Scene::destroyBuffers(VulkanBaseRenderer* renderer)
{
    for (size_t i = 0; i < vertexBuffers.size(); i++)
    {
        VkDevice device = renderer->getDevice(i);

        if (indexBuffers[i]) vkDestroyBuffer(device, indexBuffers[i], nullptr);
        if (indexBufferMemories[i]) vkFreeMemory(device, indexBufferMemories[i], nullptr);
        if (vertexBuffers[i]) vkDestroyBuffer(device, vertexBuffers[i], nullptr);
        if (vertexBufferMemories[i]) vkFreeMemory(device, vertexBufferMemories[i], nullptr);
    }

    vertexBuffers.clear();
    vertexBufferMemories.clear();
    indexBuffers.clear();
    indexBufferMemories.clear();
}

void Scene::recordDrawCommands(VkCommandBuffer commandBuffer, size_t gpuIndex)
{
    VkBuffer vertexBuffersArray[] = {vertexBuffers[gpuIndex]};
    VkDeviceSize offsets[] = {0};

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffersArray, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffers[gpuIndex], 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
}

void Scene::createDescriptorSetLayout(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                      VkDescriptorSetLayout& layout)
{
    VkDescriptorSetLayoutBinding modelUboBinding{};
    modelUboBinding.binding = 0;
    modelUboBinding.descriptorCount = 1;
    modelUboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    modelUboBinding.pImmutableSamplers = nullptr;
    modelUboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &modelUboBinding;

    if (vkCreateDescriptorSetLayout(renderer->getDevice(gpuIndex), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create scene descriptor set layout!");
    }
}

void Scene::createDescriptorPool(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                 size_t imageCount, VkDescriptorPool& pool)
{
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(imageCount);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = static_cast<uint32_t>(imageCount);

    if (vkCreateDescriptorPool(renderer->getDevice(gpuIndex), &poolInfo, nullptr, &pool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create scene descriptor pool!");
    }
}

void Scene::createDescriptorSets(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                 size_t imageCount, VkDescriptorSetLayout layout,
                                 VkDescriptorPool pool,
                                 std::vector<VkBuffer>& uniformBuffers,
                                 std::vector<VkDescriptorSet>& descriptorSets)
{
    std::vector<VkDescriptorSetLayout> layouts(imageCount, layout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(imageCount);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(imageCount);
    if (vkAllocateDescriptorSets(renderer->getDevice(gpuIndex), &allocInfo, descriptorSets.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate scene descriptor sets!");
    }

    for (size_t j = 0; j < imageCount; j++)
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[j];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(ModelUBO);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSets[j];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(renderer->getDevice(gpuIndex), 1, &descriptorWrite, 0, nullptr);
    }
}

void Scene::createUniformBuffers(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                 size_t imageCount,
                                 std::vector<VkBuffer>& buffers,
                                 std::vector<VkDeviceMemory>& memories)
{
    VkDeviceSize bufferSize = sizeof(ModelUBO);

    buffers.resize(imageCount);
    memories.resize(imageCount);

    for (size_t j = 0; j < imageCount; j++)
    {
        renderer->createBuffer(gpuIndex, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               buffers[j], memories[j]);
    }
}

void Scene::updateUniformBuffer(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                uint32_t currentImage, VkDeviceMemory memory)
{
    ModelUBO ubo{};
    ubo.model = renderer->getModelMatrix(gpuIndex);

    void* data;
    vkMapMemory(renderer->getDevice(gpuIndex), memory, 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(renderer->getDevice(gpuIndex), memory);
}
