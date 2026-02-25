#pragma once

#include "../BenchmarkCase.h"
#include "../Scene.h"
#include "../VulkanBaseRenderer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <chrono>
#include <array>

class IndirectDrawScene : public Scene
{
public:
    struct Config
    {
        int sphereSegments = 48;
        int sphereRings = 32;
        float sphereRadius = 0.15f;
        int instanceCount = 200000;
        float spreadRadius = 40.0f;
    };

    explicit IndirectDrawScene(Config config = {})
        : config(config)
    {
        sceneName = "IndirectDraw_" + std::to_string(config.instanceCount);
    }

    void generateGeometry() override;
    const char* getName() const override { return sceneName.c_str(); }

    void createBuffers(VulkanBaseRenderer* renderer) override;
    void destroyBuffers(VulkanBaseRenderer* renderer) override;
    void recordDrawCommands(VkCommandBuffer commandBuffer, size_t gpuIndex) override;

    void createDescriptorSetLayout(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                   VkDescriptorSetLayout& layout) override;

    void createDescriptorPool(VulkanBaseRenderer* renderer, size_t gpuIndex,
                              size_t imageCount, VkDescriptorPool& pool) override;

    void createDescriptorSets(VulkanBaseRenderer* renderer, size_t gpuIndex,
                              size_t imageCount, VkDescriptorSetLayout layout,
                              VkDescriptorPool pool,
                              std::vector<VkBuffer>& uniformBuffers,
                              std::vector<VkDescriptorSet>& descriptorSets) override;

    void createUniformBuffers(VulkanBaseRenderer* renderer, size_t gpuIndex,
                              size_t imageCount,
                              std::vector<VkBuffer>& buffers,
                              std::vector<VkDeviceMemory>& memories) override;

    void updateUniformBuffer(VulkanBaseRenderer* renderer, size_t gpuIndex,
                             uint32_t currentImage, VkDeviceMemory memory) override;

    std::string getVertexShaderPath() const override { return "shaders/indirect.vert.spv"; }
    std::string getFragmentShaderPath() const override { return "shaders/indirect.frag.spv"; }

    int getInstanceCount() const { return config.instanceCount; }

private:
    Config config;
    std::string sceneName;
    std::vector<glm::mat4> modelMatrices;

    // Per-GPU SSBO buffers
    std::vector<VkBuffer> modelMatrixBuffers;
    std::vector<VkDeviceMemory> modelMatrixBufferMemories;

    // Per-GPU indirect command buffers
    std::vector<VkBuffer> indirectBuffers;
    std::vector<VkDeviceMemory> indirectBufferMemories;

    void generateSphere();
    void generateInstanceData();
};

class RawThroughputCase : public BenchmarkCase
{
public:
    explicit RawThroughputCase(int instanceCount = 10000)
        : instanceCount(instanceCount) {}

    std::string getName() const override
    {
        return "RawThroughput_" + std::to_string(instanceCount);
    }

    std::string getDescription() const override
    {
        return "Indirect draw with " + std::to_string(instanceCount) + " instances - tests AFR's best case";
    }

    std::unique_ptr<Scene> createScene() const override
    {
        IndirectDrawScene::Config config;
        config.instanceCount = instanceCount;
        config.sphereSegments = 16;
        config.sphereRings = 8;
        return std::make_unique<IndirectDrawScene>(config);
    }

private:
    int instanceCount;
};
