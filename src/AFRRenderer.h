#pragma once

#include "VulkanBaseRenderer.h"

class VulkanAFRRenderer final : public VulkanBaseRenderer
{
public:
    VulkanAFRRenderer()
    {
        std::println("=== AFR Mode: Alternate Frame Rendering ===");
        std::println("GPU 0 renders frames: 0, 2, 4, 6, ...");
        std::println("GPU 1 renders frames: 1, 3, 5, 7, ...");
    }

    ~VulkanAFRRenderer() override = default;

protected:
    const char* getWindowTitle() override { return "Vulkan - AFR (Alternate Frame Rendering)"; }

    void initVulkan() override
    {
        VulkanBaseRenderer::initVulkan();
        createAFRResources();
    }

    void cleanup() override
    {
        cleanupAFRResources();
        VulkanBaseRenderer::cleanup();
    }

    void drawFrame() override;

private:
    // AFR-specific resources
    std::vector<VkRenderPass> multiGpuRenderPasses;
    std::vector<VkImage> afrRenderImages;
    std::vector<VkDeviceMemory> afrRenderImageMemories;
    std::vector<VkImageView> afrRenderImageViews;
    std::vector<VkFramebuffer> afrFramebuffers;

    // Staging buffers for cross-GPU transfer - FALLBACK
    std::vector<VkBuffer> afrStagingBuffers;
    std::vector<VkDeviceMemory> afrStagingMemories;
    std::vector<void*> afrStagingMapped; // Persistently mapped pointers

    // External memory for zero-copy cross-GPU transfer
    bool useExternalMemory = false;
    std::vector<ExternalImage> afrExternalImages;  // Per non-main GPU

    // Synchronization for alternate frame rendering
    std::vector<VkSemaphore> afrRenderCompleteSemaphores;  // Per-GPU: signaled when render finishes
    std::vector<VkSemaphore> afrPresentReadySemaphores;    // Per-frame: signaled when composite is ready
    std::vector<VkCommandBuffer> afrCompositeCommandBuffers;  // Pre-allocated composite command buffers
    std::vector<VkFence> afrCompositeFences;  // Per-frame: signaled when composite completes

    void createAFRResources();
    void cleanupAFRResources();
    void createMultiGpuRenderPasses();
    bool createExternalMemoryResources();
    void createStagingBufferResources();
    void cleanupPartialExternalMemoryResources(size_t failedIndex);
};
