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
        int numBalls = 10;
    };

    explicit TemporalDependencyScene(Config config = {})
        : config(config)
    {
        sceneName = "TemporalDependency_" + std::to_string(config.numBalls);
        pushConstantsData.numBalls = config.numBalls;
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

    std::string getVertexShaderPath() const override { return "shaders/fullscreen.vert.spv"; }
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

    void recordPostRenderPassCommands(VkCommandBuffer commandBuffer, size_t gpuIndex,
                                      VkImage renderTarget, VkImageLayout currentLayout,
                                      uint32_t width, uint32_t height) override;

private:
    Config config;
    std::string sceneName;

    struct PushConstants
    {
        int numBalls;
    };

    PushConstants pushConstantsData = {10};

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

        std::array<VkDescriptorSet, 2> descriptorSets = {VK_NULL_HANDLE, VK_NULL_HANDLE};

        VkSampler sampler = VK_NULL_HANDLE;

        uint32_t currentIndex = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    std::vector<PingPongResources> gpuResources;

    void createPingPongResources(VulkanBaseRenderer* renderer, size_t gpuIndex);
};

class TemporalDependencyCase : public BenchmarkCase
{
public:
    explicit TemporalDependencyCase(int numBalls = 5)
        : numBalls(numBalls)
    {
    }

    std::string getName() const override
    {
        return "TemporalDependency_" + std::to_string(numBalls);
    }

    std::string getDescription() const override
    {
        return "Ping-pong rendering with " + std::to_string(numBalls) +
            " balls - tests AFR's worst case (frame N+1 depends on frame N)";
    }

    std::unique_ptr<Scene> createScene() const override
    {
        TemporalDependencyScene::Config config;
        config.numBalls = numBalls;
        return std::make_unique<TemporalDependencyScene>(config);
    }

private:
    int numBalls;
};
