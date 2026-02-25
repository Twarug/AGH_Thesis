#pragma once

#include "VulkanBaseRenderer.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Single GPU Renderer - baseline for comparison
// - Uses specified GPU (default: most powerful, index 0)
// - Renders directly to swapchain
// - No multi-GPU overhead
class VulkanSingleGPURenderer final : public VulkanBaseRenderer
{
public:
    // gpuIndex: -1 = use most powerful (default), 0 = first ranked, 1 = second ranked, etc.
    explicit VulkanSingleGPURenderer(int gpuIndex = -1)
        : gpuIndex_(gpuIndex < 0 ? 0 : gpuIndex)
    {
        // Force this renderer to use only the specified GPU
        setForcedGpuIndex(gpuIndex_);

        std::println("=== Single GPU Mode: Standard Rendering ===");
        std::println("Using GPU {} to render all frames directly to swapchain", gpuIndex_);
    }

    ~VulkanSingleGPURenderer() override = default;

    int getGpuIndex() const { return gpuIndex_; }

protected:
    const char* getWindowTitle() override
    {
        static std::string title;
        title = "Vulkan - Single GPU " + std::to_string(gpuIndex_);
        return title.c_str();
    }

private:
    int gpuIndex_ = 0;

    // Single GPU renders at window resolution directly to swapchain
    VkViewport getFrameViewport(uint32_t gpuIndex) override
    {
        return {0, 0, static_cast<float>(WINDOW_WIDTH), static_cast<float>(WINDOW_HEIGHT), 0, 1};
    }

    // Use window resolution for projection aspect ratio
    glm::mat4 getProjectionMatrix(size_t gpuIndex) override
    {
        glm::mat4 proj = glm::perspective(glm::radians(45.0f),
                                          WINDOW_WIDTH / static_cast<float>(WINDOW_HEIGHT), 0.1f, 500.0f);
        proj[1][1] *= -1; // Vulkan Y-flip
        return proj;
    }

    void drawFrame() override
    {
        drawFrameSingleGPU();
    }
};
