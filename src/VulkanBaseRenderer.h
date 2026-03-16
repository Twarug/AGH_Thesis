#pragma once

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <print>
#include <vector>
#include <optional>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <set>
#include <algorithm>
#include <utility>

#include "Scene.h"
#include "Benchmark.h"
#include "FileUtils.h"

// Structure for external memory image - holds both the exported and imported resources
// Uses VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT for cross-vendor GPU sharing
struct ExternalImage {
    // Source GPU resources (where the image is created and rendered to)
    VkImage sourceImage = VK_NULL_HANDLE;
    VkDeviceMemory sourceMemory = VK_NULL_HANDLE;
    VkImageView sourceImageView = VK_NULL_HANDLE;
    size_t sourceGpuIndex = 0;

    // Target GPU resources (where the image is imported and read from)
    VkImage importedImage = VK_NULL_HANDLE;
    VkDeviceMemory importedMemory = VK_NULL_HANDLE;
    VkImageView importedImageView = VK_NULL_HANDLE;
    size_t targetGpuIndex = 0;

    // Shared host allocation (used by both source and target GPUs)
    void* hostPointer = nullptr;
    size_t allocationSize = 0;

    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
};


// Render dimensions (internal framebuffer resolution - 4K for heavy fragment testing)
// const uint32_t RENDER_WIDTH = 3840;
// const uint32_t RENDER_HEIGHT = 2160;
const uint32_t RENDER_WIDTH = 1920;
const uint32_t RENDER_HEIGHT = 1080;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

// Validation layers
#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

const std::vector validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};


struct QueueFamilyIndices
{
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete()
    {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanBaseRenderer
{
protected:
    GLFWwindow* window{};
    VkInstance instance{};
    VkDebugUtilsMessengerEXT debugMessenger{};
    VkSurfaceKHR surface{};

    // When >= 0, only use this specific GPU (used for single-GPU benchmarking)
    int forcedGpuIndex = -1;

    int mainGPU = -1;

    std::vector<VkPhysicalDevice> physicalDevices;
    std::vector<VkDevice> devices;

    std::vector<VkQueue> graphicsQueues;
    std::vector<VkQueue> presentQueues;

    // Swapchain (single for main GPU)
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapChainExtent{};
    std::vector<VkImageView> swapChainImageViews;
    std::vector<VkFramebuffer> swapChainFramebuffers;

    // Render pass and pipeline
    std::vector<VkRenderPass> renderPasses;
    std::vector<VkPipelineLayout> pipelineLayouts;
    std::vector<VkPipeline> graphicsPipelines;

    // Command pools and buffers
    std::vector<VkCommandPool> commandPools;
    std::vector<std::vector<VkCommandBuffer>> commandBuffers;

    // Scene uniform buffers (managed by scene)
    std::vector<std::vector<VkBuffer>> sceneUniformBuffers;
    std::vector<std::vector<VkDeviceMemory>> sceneUniformBuffersMemory;

    // Camera descriptors (set 0) - managed by renderer
    std::vector<VkDescriptorSetLayout> cameraDescriptorSetLayouts;
    std::vector<VkDescriptorPool> cameraDescriptorPools;
    std::vector<std::vector<VkDescriptorSet>> cameraDescriptorSets;
    std::vector<std::vector<VkBuffer>> cameraUniformBuffers;
    std::vector<std::vector<VkDeviceMemory>> cameraUniformBuffersMemory;

    // Scene descriptors (set 1) - managed by scene
    std::vector<VkDescriptorSetLayout> sceneDescriptorSetLayouts;
    std::vector<VkDescriptorPool> sceneDescriptorPools;
    std::vector<std::vector<VkDescriptorSet>> sceneDescriptorSets;

    // Depth buffers
    std::vector<VkImage> depthImages;
    std::vector<VkDeviceMemory> depthImageMemories;
    std::vector<VkImageView> depthImageViews;

    // Synchronization
    std::vector<std::vector<VkSemaphore>> imageAvailableSemaphores;
    std::vector<std::vector<VkSemaphore>> renderFinishedSemaphores;
    std::vector<std::vector<VkFence>> inFlightFences;

    size_t currentFrame = 0;
    uint64_t frameNumber = 0;

    // Scene (owned pointer)
    Scene* currentScene = nullptr;

    // Benchmarking
    Benchmark* benchmark = nullptr;
    bool benchmarkEnabled = false;
    size_t benchmarkFrameLimit = 0;  // 0 = no limit (manual close)

public:
    virtual ~VulkanBaseRenderer()
    {
        delete currentScene;
        delete benchmark;
    }

    // Set scene before calling run() - template version
    template<typename T, typename... Args>
    void setScene(Args&&... args)
    {
        delete currentScene;
        currentScene = new T(std::forward<Args>(args)...);
        currentScene->generateGeometry();
        std::println("Scene: {} ({} vertices, {} triangles)",
                     currentScene->getName(), currentScene->vertexCount(), currentScene->triangleCount());
        if (benchmark)
        {
            benchmark->setSceneName(currentScene->getName());
        }
    }

    // Set scene before calling run() - takes ownership of pre-created scene
    void setScene(std::unique_ptr<Scene> scene)
    {
        delete currentScene;
        currentScene = scene.release();
        currentScene->generateGeometry();
        std::println("Scene: {} ({} vertices, {} triangles)",
                     currentScene->getName(), currentScene->vertexCount(), currentScene->triangleCount());
        if (benchmark)
        {
            benchmark->setSceneName(currentScene->getName());
        }
    }

    // Benchmarking control
    void enableBenchmark(size_t frameLimit = 0, size_t warmupFrames = 100)
    {
        benchmark = new Benchmark(getWindowTitle(), "", warmupFrames);
        benchmarkEnabled = true;
        benchmarkFrameLimit = frameLimit;
    }

    Benchmark* getBenchmark() const { return benchmark; }

    // Public interface for Scene buffer creation
    size_t getDeviceCount() const { return devices.size(); }
    VkDevice getDevice(size_t index) const { return devices[index]; }

    void createBuffer(size_t gpuIndex, VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void copyBuffer(size_t gpuIndex, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    // Public interface for Scene pipeline creation
    VkPipelineLayout getPipelineLayout(size_t gpuIndex) const { return pipelineLayouts[gpuIndex]; }
    VkRenderPass getRenderPass(size_t gpuIndex) const { return renderPasses[gpuIndex]; }
    VkShaderModule createShaderModulePublic(size_t gpuIndex, const std::vector<char>& code)
    {
        return createShaderModule(gpuIndex, code);
    }

    // Camera/transform matrices - accessible by Scene, can be overridden by renderers
    virtual glm::mat4 getViewMatrix(size_t gpuIndex);
    virtual glm::mat4 getProjectionMatrix(size_t gpuIndex);
    virtual glm::mat4 getModelMatrix(size_t gpuIndex);
    virtual glm::vec2 getUVYRange(size_t gpuIndex);  // UV Y range [start, end] for fullscreen shaders
    float getTime() const;  // Elapsed time for animations

    // Force using a specific GPU (call before run(), -1 means use all available GPUs)
    void setForcedGpuIndex(int gpuIndex) { forcedGpuIndex = gpuIndex; }

    // Static utility to count available suitable GPUs (for benchmarking)
    static size_t countAvailableGPUs();

    virtual void run()
    {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

protected:
    virtual const char* getWindowTitle() { return "Vulkan Renderer"; }

    virtual void initWindow()
    {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        window = glfwCreateWindow(RENDER_WIDTH, RENDER_HEIGHT, getWindowTitle(), nullptr, nullptr);
    }

    virtual void initVulkan()
    {
        // Scene must be set before initialization
        if (!currentScene)
        {
            throw std::runtime_error("No scene set. Call setScene() before run().");
        }

        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevices();
        createLogicalDevices();
        createSwapChains();
        createImageViews();
        createRenderPasses();
        createDescriptorSetLayouts();  // Camera layout + Scene layout
        createGraphicsPipelines();     // Needs Scene for shader paths
        createDepthResources();
        createCommandPools();
        createSceneBuffers();          // Creates vertex, index, SSBO buffers
        createCameraUniformBuffers();  // Renderer's camera UBOs
        createSceneUniformBuffers();   // Scene's UBOs
        createDescriptorPools();       // Camera pool + Scene pool
        createDescriptorSets();        // Camera sets + Scene sets
        createCommandBuffers();
        createSyncObjects();
    }

    void createSceneBuffers()
    {
        currentScene->createBuffers(this);
    }

    virtual void mainLoop()
    {
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();

            if (benchmarkEnabled && benchmark)
            {
                benchmark->startFrame();
            }

            drawFrame();

            if (benchmarkEnabled && benchmark)
            {
                benchmark->endFrame();

                // Check frame limit
                if (benchmarkFrameLimit > 0 && benchmark->getTotalFrames() >= benchmarkFrameLimit)
                {
                    break;
                }
            }
        }

        for (size_t i = 0; i < devices.size(); i++)
        {
            vkDeviceWaitIdle(devices[i]);
        }

        // Print benchmark results
        if (benchmarkEnabled && benchmark)
        {
            benchmark->printStats();
        }
    }

    virtual void drawFrame() = 0; // Pure virtual - implemented by derived classes
    virtual VkViewport getFrameViewport(uint32_t gpuIndex) const
    {
        return {0, 0, RENDER_WIDTH, RENDER_HEIGHT, 0, 1,};
    }

    // Common single-GPU rendering path - renders directly to swapchain
    virtual void drawFrameSingleGPU();

    // Records draw commands for the scene (can be overridden to change what's drawn)
    virtual void recordDrawCommands(VkCommandBuffer commandBuffer, size_t gpuIndex, uint32_t imageIndex,
                                    const VkViewport& viewport, const VkRect2D& scissor);

    virtual void cleanup();

    // Helper functions
    void createInstance();
    void setupDebugMessenger();
    bool checkValidationLayerSupport();
    static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    void createSurface();
    void pickPhysicalDevices();
    void createLogicalDevices();
    void createSwapChains();
    void createImageViews();
    virtual void createRenderPasses();
    void createDescriptorSetLayouts();  // Creates camera layout + delegates scene layout to Scene
    void createGraphicsPipelines();     // Uses Scene's shader paths
    void createFramebuffers();
    void createCommandPools();
    void createDepthResources();
    void createCameraUniformBuffers();  // Renderer's camera UBOs
    void createSceneUniformBuffers();   // Delegates to Scene
    void createDescriptorPools();       // Creates camera pool + delegates scene pool to Scene
    void createDescriptorSets();        // Creates camera sets + delegates scene sets to Scene
    void createCommandBuffers();
    void createSyncObjects();

    void updateCameraUniformBuffer(size_t gpuIndex, uint32_t currentImage);  // Updates camera UBO
    void updateSceneUniformBuffer(size_t gpuIndex, uint32_t currentImage);   // Delegates to Scene

    // Utility functions
    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    VkShaderModule createShaderModule(size_t gpuIndex, const std::vector<char>& code);

    VkFormat findDepthFormat(size_t gpuIndex);
    VkFormat findSupportedFormat(size_t gpuIndex, const std::vector<VkFormat>& candidates,
                                 VkImageTiling tiling, VkFormatFeatureFlags features);

public:
    // Public helpers for Scene resource creation
    uint32_t findMemoryType(size_t gpuIndex, uint32_t typeFilter, VkMemoryPropertyFlags properties);

    void createImage(size_t gpuIndex, uint32_t width, uint32_t height, VkFormat format,
                     VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                     VkImage& image, VkDeviceMemory& imageMemory);
    VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

    // Execute a one-time command buffer
    VkCommandBuffer beginSingleTimeCommands(size_t gpuIndex);
    void endSingleTimeCommands(size_t gpuIndex, VkCommandBuffer commandBuffer);

    // Image layout transition helper
    void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image,
                               VkImageLayout oldLayout, VkImageLayout newLayout);

    // External memory support - for zero-copy cross-GPU rendering
    bool externalMemorySupported = false;

    // Check if external memory extensions are available
    bool checkExternalMemorySupport(VkPhysicalDevice device);

    // Check if external memory can actually be shared between two specific GPUs
    bool checkExternalMemoryCompatible(size_t sourceGpuIndex, size_t targetGpuIndex, VkFormat format);

    // Create an image with host-allocation-backed memory on the source GPU
    void createExportableImage(size_t sourceGpuIndex, uint32_t width, uint32_t height,
                               VkFormat format, VkImageUsageFlags usage,
                               VkImage& image, VkDeviceMemory& memory,
                               void*& hostPointer, size_t& allocationSize);

    // Import an external image on the target GPU (returns false if import fails)
    bool importExternalImage(size_t targetGpuIndex, ExternalImage& extImage);

    // Cleanup helpers
    void destroyExternalImage(ExternalImage& extImage);
};
