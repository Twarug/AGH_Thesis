#pragma once

#include "../BenchmarkCase.h"
#include "../Scene.h"
#include "../VulkanBaseRenderer.h"

#include <glm/glm.hpp>
#include <stdexcept>
#include <array>

class UnbalancedLoadScene : public Scene
{
public:
    struct Config
    {
        int iterations = 100;
        bool heavyOnTop = true;
    };

    explicit UnbalancedLoadScene(Config config = {})
        : config(config)
    {
        sceneName = "UnbalancedLoad_" + std::to_string(config.iterations) +
                    (config.heavyOnTop ? "_top" : "_bottom");
    }

    void generateGeometry() override {}

    const char* getName() const override { return sceneName.c_str(); }

    bool needsVertexInput() const override { return false; }
    bool needsDepthTest() const override { return false; }
    bool needsCulling() const override { return false; }

    VkPushConstantRange* getPushConstantRange() override
    {
        return &pushConstantRange;
    }

    void pushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout) override {}

    void createBuffers(VulkanBaseRenderer* renderer) override;
    void destroyBuffers(VulkanBaseRenderer* renderer) override;
    void recordDrawCommands(VkCommandBuffer commandBuffer, size_t gpuIndex) override;

    void createDescriptorSetLayout(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                   VkDescriptorSetLayout& layout) override
    {
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 0;
        layoutInfo.pBindings = nullptr;

        if (vkCreateDescriptorSetLayout(renderer->getDevice(gpuIndex), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create empty descriptor set layout!");
        }
    }

    void createDescriptorPool(VulkanBaseRenderer* renderer, size_t gpuIndex,
                              size_t imageCount, VkDescriptorPool& pool) override
    {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = static_cast<uint32_t>(imageCount);

        if (vkCreateDescriptorPool(renderer->getDevice(gpuIndex), &poolInfo, nullptr, &pool) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor pool!");
        }
    }

    void createDescriptorSets(VulkanBaseRenderer* renderer, size_t gpuIndex,
                              size_t imageCount, VkDescriptorSetLayout layout,
                              VkDescriptorPool pool,
                              std::vector<VkBuffer>& uniformBuffers,
                              std::vector<VkDescriptorSet>& descriptorSets) override
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
            throw std::runtime_error("Failed to allocate descriptor sets!");
        }
    }

    void createUniformBuffers(VulkanBaseRenderer* renderer, size_t gpuIndex,
                              size_t imageCount,
                              std::vector<VkBuffer>& buffers,
                              std::vector<VkDeviceMemory>& memories) override
    {
        buffers.clear();
        memories.clear();
    }

    void updateUniformBuffer(VulkanBaseRenderer* renderer, size_t gpuIndex,
                             uint32_t currentImage, VkDeviceMemory memory) override {}

    std::string getVertexShaderPath() const override { return "shaders/fullscreen.vert.spv"; }
    std::string getFragmentShaderPath() const override { return "shaders/fractal.frag.spv"; }

private:
    Config config;
    std::string sceneName;

    struct PushConstants
    {
        int iterations;
        float power;
        float padding[2];
    };

    PushConstants pushConstantsData = { 100, 8.0f, {0, 0} };

    VkPushConstantRange pushConstantRange = {
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(PushConstants)
    };

    // Per-GPU pipelines
    std::vector<VkPipeline> heavyPipelines;
    std::vector<VkPipeline> trivialPipelines;

    VulkanBaseRenderer* rendererRef = nullptr;

    void createPipelines(VulkanBaseRenderer* renderer);
};

class UnbalancedLoadCase : public BenchmarkCase
{
public:
    explicit UnbalancedLoadCase(int iterations = 100, bool heavyOnTop = true)
        : iterations(iterations), heavyOnTop(heavyOnTop) {}

    std::string getName() const override
    {
        return "UnbalancedLoad_" + std::to_string(iterations) +
               (heavyOnTop ? "_HeavyTop" : "_HeavyBottom");
    }

    std::string getDescription() const override
    {
        std::string side = heavyOnTop ? "top" : "bottom";
        return "Heavy fractal on " + side + " half, trivial on other - tests SFR's worst case";
    }

    std::unique_ptr<Scene> createScene() const override
    {
        UnbalancedLoadScene::Config config;
        config.iterations = iterations;
        config.heavyOnTop = heavyOnTop;
        return std::make_unique<UnbalancedLoadScene>(config);
    }

private:
    int iterations;
    bool heavyOnTop;
};
