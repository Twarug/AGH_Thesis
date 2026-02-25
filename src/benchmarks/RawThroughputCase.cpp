#include "RawThroughputCase.h"
#include "../VulkanBaseRenderer.h"

#include <cstring>

void IndirectDrawScene::generateGeometry()
{
    generateSphere();
    generateInstanceData();
}

void IndirectDrawScene::generateSphere()
{
    int segments = config.sphereSegments;
    int rings = config.sphereRings;
    float radius = config.sphereRadius;

        for (int ring = 0; ring <= rings; ring++)
    {
        float phi = glm::pi<float>() * ring / rings;
        float y = cos(phi) * radius;
        float ringRadius = sin(phi) * radius;

        for (int seg = 0; seg <= segments; seg++)
        {
            float theta = 2.0f * glm::pi<float>() * seg / segments;
            float x = cos(theta) * ringRadius;
            float z = sin(theta) * ringRadius;

            Vertex vertex;
            vertex.pos = {x, y, z};
                        vertex.color = {1.0f, 1.0f, 1.0f};
            vertices.push_back(vertex);
        }
    }

    for (int ring = 0; ring < rings; ring++)
    {
        for (int seg = 0; seg < segments; seg++)
        {
            uint16_t current = ring * (segments + 1) + seg;
            uint16_t next = current + segments + 1;

            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);

            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
}

void IndirectDrawScene::generateInstanceData()
{
    modelMatrices.resize(config.instanceCount);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> posDist(-config.spreadRadius, config.spreadRadius);
    std::uniform_real_distribution<float> rotDist(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> scaleDist(0.5f, 1.5f);

    for (int i = 0; i < config.instanceCount; i++)
    {
        auto model = glm::mat4(1.0f);

                glm::vec3 pos(posDist(rng), posDist(rng), posDist(rng));
        model = glm::translate(model, pos);

                model = glm::rotate(model, rotDist(rng), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, rotDist(rng), glm::vec3(0.0f, 1.0f, 0.0f));

                float scale = scaleDist(rng);
        model = glm::scale(model, glm::vec3(scale));

        modelMatrices[i] = model;
    }
}

void IndirectDrawScene::createBuffers(VulkanBaseRenderer* renderer)
{
    Scene::createBuffers(renderer);

    size_t deviceCount = renderer->getDeviceCount();

    modelMatrixBuffers.resize(deviceCount);
    modelMatrixBufferMemories.resize(deviceCount);

    VkDeviceSize ssboSize = sizeof(glm::mat4) * config.instanceCount;

    for (size_t i = 0; i < deviceCount; i++)
    {
                VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;

        renderer->createBuffer(i, ssboSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               stagingBuffer, stagingMemory);

                void* data;
        vkMapMemory(renderer->getDevice(i), stagingMemory, 0, ssboSize, 0, &data);
        memcpy(data, modelMatrices.data(), ssboSize);
        vkUnmapMemory(renderer->getDevice(i), stagingMemory);

                renderer->createBuffer(i, ssboSize,
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               modelMatrixBuffers[i], modelMatrixBufferMemories[i]);

        renderer->copyBuffer(i, stagingBuffer, modelMatrixBuffers[i], ssboSize);

        vkDestroyBuffer(renderer->getDevice(i), stagingBuffer, nullptr);
        vkFreeMemory(renderer->getDevice(i), stagingMemory, nullptr);
    }

        indirectBuffers.resize(deviceCount);
    indirectBufferMemories.resize(deviceCount);

    VkDeviceSize indirectSize = sizeof(VkDrawIndexedIndirectCommand) * config.instanceCount;

        std::vector<VkDrawIndexedIndirectCommand> indirectCommands(config.instanceCount);
    for (int i = 0; i < config.instanceCount; i++)
    {
        indirectCommands[i].indexCount = static_cast<uint32_t>(indices.size());
        indirectCommands[i].instanceCount = 1;
        indirectCommands[i].firstIndex = 0;
        indirectCommands[i].vertexOffset = 0;
        indirectCommands[i].firstInstance = 0;
    }

    for (size_t i = 0; i < deviceCount; i++)
    {
                VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;

        renderer->createBuffer(i, indirectSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               stagingBuffer, stagingMemory);

        void* data;
        vkMapMemory(renderer->getDevice(i), stagingMemory, 0, indirectSize, 0, &data);
        memcpy(data, indirectCommands.data(), indirectSize);
        vkUnmapMemory(renderer->getDevice(i), stagingMemory);

                renderer->createBuffer(i, indirectSize,
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               indirectBuffers[i], indirectBufferMemories[i]);

        renderer->copyBuffer(i, stagingBuffer, indirectBuffers[i], indirectSize);

        vkDestroyBuffer(renderer->getDevice(i), stagingBuffer, nullptr);
        vkFreeMemory(renderer->getDevice(i), stagingMemory, nullptr);
    }
}

void IndirectDrawScene::destroyBuffers(VulkanBaseRenderer* renderer)
{
    for (size_t i = 0; i < indirectBuffers.size(); i++)
    {
        VkDevice device = renderer->getDevice(i);
        if (indirectBuffers[i]) vkDestroyBuffer(device, indirectBuffers[i], nullptr);
        if (indirectBufferMemories[i]) vkFreeMemory(device, indirectBufferMemories[i], nullptr);
    }
    indirectBuffers.clear();
    indirectBufferMemories.clear();

    for (size_t i = 0; i < modelMatrixBuffers.size(); i++)
    {
        VkDevice device = renderer->getDevice(i);
        if (modelMatrixBuffers[i]) vkDestroyBuffer(device, modelMatrixBuffers[i], nullptr);
        if (modelMatrixBufferMemories[i]) vkFreeMemory(device, modelMatrixBufferMemories[i], nullptr);
    }
    modelMatrixBuffers.clear();
    modelMatrixBufferMemories.clear();

    Scene::destroyBuffers(renderer);
}

void IndirectDrawScene::recordDrawCommands(VkCommandBuffer commandBuffer, size_t gpuIndex)
{
    VkBuffer vertexBuffersArray[] = {vertexBuffers[gpuIndex]};
    VkDeviceSize offsets[] = {0};

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffersArray, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffers[gpuIndex], 0, VK_INDEX_TYPE_UINT16);

    vkCmdDrawIndexedIndirect(
        commandBuffer,
        indirectBuffers[gpuIndex],
        0,
        config.instanceCount,
        sizeof(VkDrawIndexedIndirectCommand)
    );
}

void IndirectDrawScene::createDescriptorSetLayout(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                                  VkDescriptorSetLayout& layout)
{
    VkDescriptorSetLayoutBinding ssboBinding{};
    ssboBinding.binding = 0;
    ssboBinding.descriptorCount = 1;
    ssboBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ssboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    ssboBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &ssboBinding;

    if (vkCreateDescriptorSetLayout(renderer->getDevice(gpuIndex), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create scene descriptor set layout!");
    }
}

void IndirectDrawScene::createDescriptorPool(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                             size_t imageCount, VkDescriptorPool& pool)
{
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
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

void IndirectDrawScene::createDescriptorSets(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                             size_t imageCount, VkDescriptorSetLayout layout,
                                             VkDescriptorPool pool,
                                             std::vector<VkBuffer>& uniformBuffers,
                                             std::vector<VkDescriptorSet>& descriptorSets)
{
    std::vector layouts(imageCount, layout);

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
        VkDescriptorBufferInfo ssboInfo{};
        ssboInfo.buffer = modelMatrixBuffers[gpuIndex];
        ssboInfo.offset = 0;
        ssboInfo.range = sizeof(glm::mat4) * config.instanceCount;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSets[j];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &ssboInfo;

        vkUpdateDescriptorSets(renderer->getDevice(gpuIndex), 1, &descriptorWrite, 0, nullptr);
    }
}

void IndirectDrawScene::createUniformBuffers(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                             size_t imageCount,
                                             std::vector<VkBuffer>& buffers,
                                             std::vector<VkDeviceMemory>& memories)
{
    buffers.clear();
    memories.clear();
}

void IndirectDrawScene::updateUniformBuffer(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                            uint32_t currentImage, VkDeviceMemory memory) {}


REGISTER_BENCHMARK_CASE(RawThroughputCase)
