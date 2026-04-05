#pragma once

#include "VulkanBaseRenderer.h"

class VulkanSFRRenderer final : public VulkanBaseRenderer
{
public:
    VulkanSFRRenderer()
    {
        std::println("=== SFR Mode: Split Frame Rendering ===");
        std::println("GPU 0 renders: Top half (y=0 to y={})", RENDER_HEIGHT / 2);
        std::println("GPU 1 renders: Bottom half (y={} to y={})", RENDER_HEIGHT / 2, RENDER_HEIGHT);
    }

    ~VulkanSFRRenderer() override = default;

protected:
    const char* getWindowTitle() override { return "Vulkan - SFR (Split Frame Rendering)"; }

    void initVulkan() override
    {
        VulkanBaseRenderer::initVulkan();
        createSFRResources();
    }

    void cleanup() override
    {
        cleanupSFRResources();
        VulkanBaseRenderer::cleanup();
    }

    void drawFrame() override;

    VkViewport getFrameViewport(uint32_t gpuIndex) const override;

    glm::mat4 getProjectionMatrix(size_t gpuIndex) override;

    glm::vec2 getUVYRange(size_t gpuIndex) override;

private:
    // SFR-specific resources
    std::vector<VkRenderPass> multiGpuRenderPasses;
    std::vector<VkImage> sfrRenderImages;
    std::vector<VkDeviceMemory> sfrRenderImageMemories;
    std::vector<VkImageView> sfrRenderImageViews;
    std::vector<VkFramebuffer> sfrFramebuffers;

    // Staging buffers for cross-GPU transfer - FALLBACK
    std::vector<VkBuffer> sfrStagingBuffers;
    std::vector<VkDeviceMemory> sfrStagingMemories;
    std::vector<void*> sfrStagingMapped; // Persistently mapped pointers

    // External memory for zero-copy cross-GPU transfer
    bool useExternalMemory = false;
    std::vector<ExternalImage> sfrExternalImages;  // Per non-main GPU

    // Synchronization for parallel rendering
    std::vector<VkSemaphore> sfrRenderCompleteSemaphores;  // Per-GPU: signaled when render finishes
    std::vector<VkSemaphore> sfrPresentReadySemaphores;    // Per-frame: signaled when composite is ready
    std::vector<VkCommandBuffer> sfrCompositeCommandBuffers;  // Pre-allocated composite command buffers
    std::vector<VkFence> sfrCompositeFences;  // Per-frame: signaled when composite completes

    void createSFRResources();
    void cleanupSFRResources();
    void createMultiGpuRenderPasses();
    bool createExternalMemoryResources();
    void createStagingBufferResources();
    void cleanupPartialExternalMemoryResources(size_t failedIndex);
    void recordSFRRenderCommands(size_t gpuIndex, VkCommandBuffer commandBuffer,
                                 uint32_t imageIndex, uint32_t yOffset, uint32_t renderHeight);
};
