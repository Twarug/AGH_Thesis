#pragma once

#include "../BenchmarkCase.h"
#include "../Scene.h"
#include "../VulkanBaseRenderer.h"

#include <glm/glm.hpp>
#include <stdexcept>
#include <array>

class TemporalDependencyScene : public Scene
{
public:
    struct Config
    {
        int iterations = 50;
        float decayRate = 0.95f;
    };

    explicit TemporalDependencyScene(Config config = {})
        : config(config)
    {
        sceneName = "TemporalDependency_" + std::to_string(config.iterations);
        pushConstantsData.iterations = config.iterations;
        pushConstantsData.decayRate = config.decayRate;
    }

    void generateGeometry() override
    {
        // No geometry needed - fullscreen triangle generated in shader
    }

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
        vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(PushConstants), &pushConstantsData);
    }

    std::string getVertexShaderPath() const override { return "shaders/temporal.vert.spv"; }
    std::string getFragmentShaderPath() const override { return "shaders/temporal.frag.spv"; }

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

    void preFrameUpdate(VulkanBaseRenderer* renderer, size_t gpuIndex);
    void postFrameUpdate(VulkanBaseRenderer* renderer, size_t gpuIndex);

    VkFramebuffer getCurrentFramebuffer(size_t gpuIndex) const;

    VkDescriptorSet getCurrentDescriptorSet(size_t gpuIndex) const;

private:
    Config config;
    std::string sceneName;

    struct PushConstants
    {
        int iterations;
        float decayRate;
        float padding[2];
    };

    PushConstants pushConstantsData = { 50, 0.95f, {0, 0} };

    VkPushConstantRange pushConstantRange = {
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(PushConstants)
    };

    VulkanBaseRenderer* rendererRef = nullptr;

    struct PingPongResources
    {
        std::array<VkImage, 2> images = {VK_NULL_HANDLE, VK_NULL_HANDLE};
        std::array<VkDeviceMemory, 2> imageMemories = {VK_NULL_HANDLE, VK_NULL_HANDLE};
        std::array<VkImageView, 2> imageViews = {VK_NULL_HANDLE, VK_NULL_HANDLE};
        std::array<VkFramebuffer, 2> framebuffers = {VK_NULL_HANDLE, VK_NULL_HANDLE};

        std::array<VkDescriptorSet, 2> descriptorSets = {VK_NULL_HANDLE, VK_NULL_HANDLE};

        VkSampler sampler = VK_NULL_HANDLE;

        uint32_t currentIndex = 0;
    };

    std::vector<PingPongResources> gpuResources;

    std::vector<VkRenderPass> temporalRenderPasses;

    void createPingPongResources(VulkanBaseRenderer* renderer, size_t gpuIndex);
    void createTemporalRenderPass(VulkanBaseRenderer* renderer, size_t gpuIndex);
};

class TemporalDependencyCase : public BenchmarkCase
{
public:
    explicit TemporalDependencyCase(int iterations = 50)
        : iterations(iterations) {}

    std::string getName() const override
    {
        return "TemporalDependency_" + std::to_string(iterations);
    }

    std::string getDescription() const override
    {
        return "Ping-pong rendering with " + std::to_string(iterations) +
               " iterations - tests AFR's worst case (frame N+1 depends on frame N)";
    }

    std::unique_ptr<Scene> createScene() const override
    {
        TemporalDependencyScene::Config config;
        config.iterations = iterations;
        return std::make_unique<TemporalDependencyScene>(config);
    }

    size_t getFrameCount() const override
    {
        return 1000;
    }

private:
    int iterations;
};
