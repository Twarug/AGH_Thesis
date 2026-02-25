#pragma once

#include "VulkanBaseRenderer.h"

// SFR: Split Frame Rendering
// - Screen divided vertically among GPUs
// - GPU 0 renders top half
// - GPU 1 renders bottom half
// - Each GPU renders to its own render target
// - Results composited into single swapchain
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

    VkViewport getFrameViewport(uint32_t gpuIndex) override;

    // Override projection matrix for asymmetric frustum in SFR mode
    glm::mat4 getProjectionMatrix(size_t gpuIndex) override;

private:
    // SFR-specific resources
    std::vector<VkRenderPass> multiGpuRenderPasses;  // For off-screen rendering (TRANSFER_SRC_OPTIMAL)
    std::vector<VkImage> sfrRenderImages;
    std::vector<VkDeviceMemory> sfrRenderImageMemories;
    std::vector<VkImageView> sfrRenderImageViews;
    std::vector<VkFramebuffer> sfrFramebuffers;

    // Staging buffers for cross-GPU transfer (host-visible) - FALLBACK when external memory not available
    std::vector<VkBuffer> sfrStagingBuffers;
    std::vector<VkDeviceMemory> sfrStagingMemories;
    std::vector<void*> sfrStagingMapped; // Persistently mapped pointers

    // External memory for zero-copy cross-GPU transfer (preferred when available)
    bool useExternalMemory = false;
    std::vector<ExternalImage> sfrExternalImages;  // Per non-main GPU
    std::vector<ExternalSemaphore> sfrExternalSemaphores;  // For cross-GPU synchronization

    // Composite image on main GPU for blit to swapchain (render resolution -> window resolution)
    VkImage sfrCompositeImage = VK_NULL_HANDLE;
    VkDeviceMemory sfrCompositeImageMemory = VK_NULL_HANDLE;

    // Synchronization for parallel rendering
    std::vector<VkSemaphore> sfrRenderCompleteSemaphores;  // Per-GPU: signaled when render finishes
    std::vector<VkSemaphore> sfrPresentReadySemaphores;    // Per-frame: signaled when composite is ready
    std::vector<VkCommandBuffer> sfrCompositeCommandBuffers;  // Pre-allocated composite command buffers
    std::vector<VkFence> sfrCompositeFences;  // Per-frame: signaled when composite completes

    void createSFRResources();
    void cleanupSFRResources();
    void createMultiGpuRenderPasses();
    bool createExternalMemoryResources();  // Returns false if external memory import fails
    void createStagingBufferResources();
    void cleanupPartialExternalMemoryResources(size_t failedIndex);  // Cleanup after failed external memory setup
    void recordSFRRenderCommands(size_t gpuIndex, VkCommandBuffer commandBuffer,
                                 uint32_t imageIndex, uint32_t yOffset, uint32_t renderHeight);

    VkCommandBuffer beginSingleTimeCommands(size_t gpuIndex);
    void endSingleTimeCommands(VkCommandBuffer commandBuffer, size_t gpuIndex, uint32_t imageIndex,
                               const std::vector<VkSemaphore>& waitSemaphores);
};
