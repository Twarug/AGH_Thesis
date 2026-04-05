#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cmath>
#include <memory>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "RendererData.h"

// Forward declaration
class VulkanBaseRenderer;

// Base Scene class - subclass to create different scenes
class Scene
{
public:
    virtual ~Scene() = default;

    // Override to generate scene geometry
    virtual void generateGeometry() = 0;

    // Override to return scene name
    virtual const char* getName() const = 0;

    // ========================================================================
    // GPU Buffer Management
    // ========================================================================

    // Create GPU buffers for all devices (vertex, index, and scene-specific buffers)
    virtual void createBuffers(VulkanBaseRenderer* renderer);

    // Destroy GPU buffers
    virtual void destroyBuffers(VulkanBaseRenderer* renderer);

    // Record draw commands to command buffer
    virtual void recordDrawCommands(VkCommandBuffer commandBuffer, size_t gpuIndex);

    // ========================================================================
    // Descriptor Management (set 1) - override for custom descriptors like SSBO
    // Note: Camera descriptors (set 0) are managed by the Renderer
    // ========================================================================

    // Create descriptor set layout for this scene's resources (set 1)
    // Default: model matrix UBO at binding 0
    virtual void createDescriptorSetLayout(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                           VkDescriptorSetLayout& layout);

    // Create descriptor pool for scene resources
    virtual void createDescriptorPool(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                      size_t imageCount, VkDescriptorPool& pool);

    // Create and configure descriptor sets for scene resources
    virtual void createDescriptorSets(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                      size_t imageCount, VkDescriptorSetLayout layout,
                                      VkDescriptorPool pool,
                                      std::vector<VkBuffer>& uniformBuffers,
                                      std::vector<VkDescriptorSet>& descriptorSets);

    // ========================================================================
    // Scene Uniform Buffer Management (set 1)
    // Note: Camera UBOs (set 0) are managed by the Renderer
    // ========================================================================

    // Create uniform buffers for scene data (model matrices, etc.)
    virtual void createUniformBuffers(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                      size_t imageCount,
                                      std::vector<VkBuffer>& buffers,
                                      std::vector<VkDeviceMemory>& memories);

    // Update scene uniform buffer (called each frame)
    // Default: updates model matrix from renderer->getModelMatrix()
    virtual void updateUniformBuffer(VulkanBaseRenderer* renderer, size_t gpuIndex,
                                     uint32_t currentImage, VkDeviceMemory memory);

    // ========================================================================
    // Shader Configuration
    // ========================================================================

    virtual std::string getVertexShaderPath() const = 0;
    virtual std::string getFragmentShaderPath() const = 0;

    // ========================================================================
    // Push Constants (optional) - override to provide push constant data
    // ========================================================================

    // Return push constant range, or nullptr if not used
    virtual VkPushConstantRange* getPushConstantRange() { return nullptr; }

    // Called before draw to push constants - override to push your data
    virtual void pushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout) {}

    // ========================================================================
    // Pipeline Configuration (optional)
    // ========================================================================

    // Override to disable depth testing (e.g., for fullscreen passes)
    virtual bool needsDepthTest() const { return true; }

    // Override to disable backface culling
    virtual bool needsCulling() const { return true; }

    // Called after vkCmdEndRenderPass - override to copy render target to ping-pong buffers etc.
    virtual void recordPostRenderPassCommands(VkCommandBuffer commandBuffer, size_t gpuIndex,
                                              VkImage renderTarget, VkImageLayout currentLayout,
                                              uint32_t width, uint32_t height) {}

    // Override to use custom vertex input (false = no vertex buffers, generate in shader)
    virtual bool needsVertexInput() const { return true; }

    // ========================================================================
    // Stats
    // ========================================================================

    size_t triangleCount() const { return indices.size() / 3; }
    size_t vertexCount() const { return vertices.size(); }

    // Access to geometry (for composite scenes)
    const std::vector<Vertex>& getVertices() const { return vertices; }
    const std::vector<uint16_t>& getIndices() const { return indices; }

protected:
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

    // Per-GPU buffers
    std::vector<VkBuffer> vertexBuffers;
    std::vector<VkDeviceMemory> vertexBufferMemories;
    std::vector<VkBuffer> indexBuffers;
    std::vector<VkDeviceMemory> indexBufferMemories;
};
