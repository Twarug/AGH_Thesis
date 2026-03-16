#pragma once

#include "../BenchmarkCase.h"
#include "../Scene.h"
#include "../VulkanBaseRenderer.h"

#include <glm/glm.hpp>
#include <stdexcept>

class FragmentOverloadScene : public Scene
{
public:
    struct Config
    {
        int iterations = 100;
        float fractalPower = 8.0f;
    };

    explicit FragmentOverloadScene(Config config = {})
        : config(config)
    {
        sceneName = "FragmentOverload_" + std::to_string(config.iterations);
        pushConstantsData.iterations = config.iterations;
        pushConstantsData.power = config.fractalPower;
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

    void pushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout) override
    {
        vkCmdPushConstants(commandBuffer, layout,
                          VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(PushConstants), &pushConstantsData);
    }

    void createBuffers(VulkanBaseRenderer* renderer) override {}

    void destroyBuffers(VulkanBaseRenderer* renderer) override {}

    void recordDrawCommands(VkCommandBuffer commandBuffer, size_t gpuIndex) override
    {
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }

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
        std::vector layouts(imageCount, layout);

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

    int getIterations() const { return config.iterations; }
    void setIterations(int iters) { config.iterations = iters; pushConstantsData.iterations = iters; }

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
};

class FragmentOverloadCase : public BenchmarkCase
{
public:
    explicit FragmentOverloadCase(int iterations = 100)
        : iterations(iterations) {}

    [[nodiscard]] std::string getName() const override
    {
        return "FragmentOverload_" + std::to_string(iterations);
    }

    [[nodiscard]] std::string getDescription() const override
    {
        return "Ray-marched fractal with " + std::to_string(iterations) + " iterations - tests SFR's best case";
    }

    [[nodiscard]] std::unique_ptr<Scene> createScene() const override
    {
        FragmentOverloadScene::Config config;
        config.iterations = iterations;
        return std::make_unique<FragmentOverloadScene>(config);
    }

private:
    int iterations;
};
