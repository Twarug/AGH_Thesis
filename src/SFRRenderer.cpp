#include "SFRRenderer.h"

void VulkanSFRRenderer::createMultiGpuRenderPasses()
{
    multiGpuRenderPasses.resize(devices.size());

    for (size_t i = 0; i < devices.size(); i++)
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapChainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = findDepthFormat(i);
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(devices[i], &renderPassInfo, nullptr, &multiGpuRenderPasses[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create multi-GPU render pass!");
        }
    }
}

void VulkanSFRRenderer::createSFRResources()
{
    createMultiGpuRenderPasses();

    useExternalMemory = externalMemorySupported && devices.size() > 1;

    if (useExternalMemory)
    {
        if (!checkExternalMemoryCompatible(1, 0, swapChainImageFormat))
        {
            std::println("SFR: External memory not compatible between GPUs, using staging buffers");
            useExternalMemory = false;
        }
    }

    if (useExternalMemory)
    {
        std::println("SFR: Attempting external memory for zero-copy cross-GPU transfer...");
        if (!createExternalMemoryResources())
        {
            std::println("SFR: External memory import failed, falling back to staging buffers");
            useExternalMemory = false;
            createStagingBufferResources();
        }
    }
    else
    {
        std::println("SFR: Using staging buffers for cross-GPU transfer (CPU memcpy)");
        createStagingBufferResources();
    }

    sfrRenderCompleteSemaphores.resize(devices.size());
    for (size_t i = 0; i < devices.size(); i++)
    {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore(devices[i], &semaphoreInfo, nullptr, &sfrRenderCompleteSemaphores[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create SFR render complete semaphore!");
        }
    }

    size_t swapchainImageCount = swapChainImages.size();
    sfrPresentReadySemaphores.resize(swapchainImageCount);
    for (size_t i = 0; i < swapchainImageCount; i++)
    {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore(devices[mainGPU], &semaphoreInfo, nullptr, &sfrPresentReadySemaphores[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create SFR present ready semaphore!");
        }
    }

    sfrCompositeCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPools[mainGPU];
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateCommandBuffers(devices[mainGPU], &allocInfo, sfrCompositeCommandBuffers.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate SFR composite command buffers!");
    }

    sfrCompositeFences.resize(MAX_FRAMES_IN_FLIGHT);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(devices[mainGPU], &fenceInfo, nullptr, &sfrCompositeFences[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create SFR composite fence!");
        }
    }

    std::println("Created SFR composite image and synchronization resources");
}

bool VulkanSFRRenderer::createExternalMemoryResources()
{
    uint32_t sectionHeight = RENDER_HEIGHT / devices.size();

    sfrRenderImages.resize(devices.size());
    sfrRenderImageMemories.resize(devices.size());
    sfrRenderImageViews.resize(devices.size());
    sfrFramebuffers.resize(devices.size());

    sfrExternalImages.resize(devices.size());

    for (size_t i = 0; i < devices.size(); i++)
    {
        if (i == mainGPU)
        {
            createImage(i, RENDER_WIDTH, sectionHeight, swapChainImageFormat,
                        VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        sfrRenderImages[i], sfrRenderImageMemories[i]);
        }
        else
        {
            // Render target: device-local, optimal-tiled (LINEAR tiling can't reliably be used as color attachment)
            createImage(i, RENDER_WIDTH, sectionHeight, swapChainImageFormat,
                        VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        sfrRenderImages[i], sfrRenderImageMemories[i]);

            // External memory image: host-memory, linear-tiled (for cross-GPU transfer)
            createExportableImage(i, RENDER_WIDTH, sectionHeight, swapChainImageFormat,
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                  sfrExternalImages[i].sourceImage, sfrExternalImages[i].sourceMemory,
                                  sfrExternalImages[i].hostPointer, sfrExternalImages[i].allocationSize);

            sfrExternalImages[i].sourceGpuIndex = i;
            sfrExternalImages[i].format = swapChainImageFormat;
            sfrExternalImages[i].width = RENDER_WIDTH;
            sfrExternalImages[i].height = sectionHeight;

            if (!importExternalImage(mainGPU, sfrExternalImages[i]))
            {
                cleanupPartialExternalMemoryResources(i);
                return false;
            }
        }

        sfrRenderImageViews[i] = createImageView(devices[i], sfrRenderImages[i],
                                                 swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        std::array attachments = {
            sfrRenderImageViews[i],
            depthImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = multiGpuRenderPasses[i];
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = RENDER_WIDTH;
        framebufferInfo.height = sectionHeight;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(devices[i], &framebufferInfo, nullptr, &sfrFramebuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create SFR framebuffer!");
        }

        std::println("Created SFR render target {} (full {}x{}, renders section {} pixels){}",
                     i, RENDER_WIDTH, RENDER_HEIGHT, sectionHeight, (i != mainGPU ? " (external memory)" : ""));
    }

    std::println("SFR: Successfully using external memory for zero-copy cross-GPU transfer");
    return true;
}

void VulkanSFRRenderer::createStagingBufferResources()
{
    uint32_t sectionHeight = RENDER_HEIGHT / devices.size();

    sfrRenderImages.resize(devices.size());
    sfrRenderImageMemories.resize(devices.size());
    sfrRenderImageViews.resize(devices.size());
    sfrFramebuffers.resize(devices.size());
    sfrStagingBuffers.resize(devices.size());
    sfrStagingMemories.resize(devices.size());
    sfrStagingMapped.resize(devices.size());

    for (size_t i = 0; i < devices.size(); i++)
    {
        VkViewport viewport = getFrameViewport(i);

        createImage(i, viewport.width, viewport.height, swapChainImageFormat,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    sfrRenderImages[i], sfrRenderImageMemories[i]);

        sfrRenderImageViews[i] = createImageView(devices[i], sfrRenderImages[i],
                                                 swapChainImageFormat,
                                                 VK_IMAGE_ASPECT_COLOR_BIT);

        std::array attachments = {
            sfrRenderImageViews[i],
            depthImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = multiGpuRenderPasses[i];
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = viewport.width;
        framebufferInfo.height = viewport.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(devices[i], &framebufferInfo, nullptr, &sfrFramebuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create SFR framebuffer!");
        }

        VkDeviceSize bufferSize = RENDER_WIDTH * sectionHeight * 4;
        if (i == 0 && devices.size() > 1)
        {
            bufferSize = RENDER_WIDTH * RENDER_HEIGHT * 4;
        }
        createBuffer(i, bufferSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     sfrStagingBuffers[i], sfrStagingMemories[i]);

        vkMapMemory(devices[i], sfrStagingMemories[i], 0, bufferSize, 0, &sfrStagingMapped[i]);

        std::println("Created SFR render target {} (full {}x{}, renders section {} pixels)",
                     i, RENDER_WIDTH, RENDER_HEIGHT, sectionHeight);
    }
}

void VulkanSFRRenderer::cleanupPartialExternalMemoryResources(size_t failedIndex)
{
    for (size_t i = 0; i <= failedIndex && i < devices.size(); i++)
    {
        if (sfrFramebuffers.size() > i && sfrFramebuffers[i] != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(devices[i], sfrFramebuffers[i], nullptr);
        }
        if (sfrRenderImageViews.size() > i && sfrRenderImageViews[i] != VK_NULL_HANDLE)
        {
            vkDestroyImageView(devices[i], sfrRenderImageViews[i], nullptr);
        }
        if (sfrRenderImages.size() > i && sfrRenderImages[i] != VK_NULL_HANDLE)
        {
            vkDestroyImage(devices[i], sfrRenderImages[i], nullptr);
        }
        if (sfrRenderImageMemories.size() > i && sfrRenderImageMemories[i] != VK_NULL_HANDLE)
        {
            vkFreeMemory(devices[i], sfrRenderImageMemories[i], nullptr);
        }
    }

    for (size_t i = 0; i < sfrExternalImages.size(); i++)
    {
        destroyExternalImage(sfrExternalImages[i]);
    }

    sfrFramebuffers.clear();
    sfrRenderImageViews.clear();
    sfrRenderImages.clear();
    sfrRenderImageMemories.clear();
    sfrExternalImages.clear();
}

void VulkanSFRRenderer::cleanupSFRResources()
{
    for (size_t i = 0; i < devices.size(); i++)
    {
        vkDeviceWaitIdle(devices[i]);
    }

    for (size_t i = 0; i < sfrRenderCompleteSemaphores.size(); i++)
    {
        if (sfrRenderCompleteSemaphores[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(devices[i], sfrRenderCompleteSemaphores[i], nullptr);
        }
    }
    sfrRenderCompleteSemaphores.clear();

    for (size_t i = 0; i < sfrPresentReadySemaphores.size(); i++)
    {
        if (sfrPresentReadySemaphores[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(devices[mainGPU], sfrPresentReadySemaphores[i], nullptr);
        }
    }
    sfrPresentReadySemaphores.clear();

    if (!sfrCompositeCommandBuffers.empty())
    {
        vkFreeCommandBuffers(devices[mainGPU], commandPools[mainGPU],
                             static_cast<uint32_t>(sfrCompositeCommandBuffers.size()),
                             sfrCompositeCommandBuffers.data());
        sfrCompositeCommandBuffers.clear();
    }

    for (size_t i = 0; i < sfrCompositeFences.size(); i++)
    {
        if (sfrCompositeFences[i] != VK_NULL_HANDLE)
        {
            vkDestroyFence(devices[mainGPU], sfrCompositeFences[i], nullptr);
        }
    }
    sfrCompositeFences.clear();

    for (size_t i = 0; i < sfrStagingBuffers.size(); i++)
    {
        if (sfrStagingMapped[i])
        {
            vkUnmapMemory(devices[i], sfrStagingMemories[i]);
        }
        if (sfrStagingBuffers[i] != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(devices[i], sfrStagingBuffers[i], nullptr);
        }
        if (sfrStagingMemories[i] != VK_NULL_HANDLE)
        {
            vkFreeMemory(devices[i], sfrStagingMemories[i], nullptr);
        }
    }
    sfrStagingMapped.clear();
    sfrStagingMemories.clear();
    sfrStagingBuffers.clear();

    for (size_t i = 0; i < sfrFramebuffers.size(); i++)
    {
        if (sfrFramebuffers[i] != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(devices[i], sfrFramebuffers[i], nullptr);
        }
    }
    sfrFramebuffers.clear();

    for (size_t i = 0; i < sfrRenderImageViews.size(); i++)
    {
        if (sfrRenderImageViews[i] != VK_NULL_HANDLE)
        {
            vkDestroyImageView(devices[i], sfrRenderImageViews[i], nullptr);
        }
    }
    sfrRenderImageViews.clear();

    for (size_t i = 1; i < sfrExternalImages.size(); i++)
    {
        if (sfrExternalImages[i].importedImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(devices[mainGPU], sfrExternalImages[i].importedImage, nullptr);
            vkFreeMemory(devices[mainGPU], sfrExternalImages[i].importedMemory, nullptr);
        }
        if (sfrExternalImages[i].sourceImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(devices[i], sfrExternalImages[i].sourceImage, nullptr);
            vkFreeMemory(devices[i], sfrExternalImages[i].sourceMemory, nullptr);
        }
        if (sfrExternalImages[i].hostPointer != nullptr)
        {
#ifdef _WIN32
            _aligned_free(sfrExternalImages[i].hostPointer);
#else
            free(sfrExternalImages[i].hostPointer);
#endif
            sfrExternalImages[i].hostPointer = nullptr;
        }
    }
    sfrExternalImages.clear();

    for (size_t i = 0; i < sfrRenderImages.size(); i++)
    {
        if (sfrRenderImages[i] != VK_NULL_HANDLE)
        {
            vkDestroyImage(devices[i], sfrRenderImages[i], nullptr);
        }
        if (sfrRenderImageMemories[i] != VK_NULL_HANDLE)
        {
            vkFreeMemory(devices[i], sfrRenderImageMemories[i], nullptr);
        }
    }
    sfrRenderImages.clear();
    sfrRenderImageMemories.clear();

    for (size_t i = 0; i < multiGpuRenderPasses.size(); i++)
    {
        if (multiGpuRenderPasses[i]) vkDestroyRenderPass(devices[i], multiGpuRenderPasses[i], nullptr);
    }
    multiGpuRenderPasses.clear();
}

VkViewport VulkanSFRRenderer::getFrameViewport(uint32_t gpuIndex) const
{
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(RENDER_WIDTH);
    viewport.height = static_cast<float>(RENDER_HEIGHT) / devices.size();
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    return viewport;
}

glm::mat4 VulkanSFRRenderer::getProjectionMatrix(size_t gpuIndex)
{
    float zNear = 0.1;
    float zFar = 500;
    float fovY = 45;
    float aspect = static_cast<float>(RENDER_WIDTH) / static_cast<float>(RENDER_HEIGHT);

    // 1. Calculate the full height/width at the near plane
    float halfHeight = zNear * glm::tan(glm::radians(fovY) * 0.5f);
    float halfWidth = halfHeight * aspect;

    // Standard bounds (GLM/OpenGL style: bottom is negative, top is positive)
    float fullTop = halfHeight;
    float fullBottom = -halfHeight;
    float left = -halfWidth;
    float right = halfWidth;

    // 2. Calculate the height of a single strip
    float totalHeight = fullTop - fullBottom;
    float stripHeight = totalHeight / static_cast<float>(devices.size());

    // 3. Define the vertical bounds for this strip
    // Index 0 is the top strip, so we start from fullTop and subtract
    float stripTop = fullTop - (gpuIndex * stripHeight);
    float stripBottom = fullTop - ((gpuIndex + 1) * stripHeight);

    // 4. Create the off-center frustum
    // glm::frustum(left, right, bottom, top, near, far)
    glm::mat4 proj = glm::frustum(left, right, stripBottom, stripTop, zNear, zFar);

    // 5. Vulkan Y-Flip: Invert the Y axis to match Vulkan's clip space (Y-down)
    // For asymmetric frustums, both the scale (P[1][1]) and the off-center offset (P[2][1])
    // must be negated. For symmetric frustums P[2][1] is 0 so only P[1][1] matters,
    // but here the strip bounds are asymmetric (e.g. bottom=0, top=halfH).
    proj[1][1] *= -1.0f;
    proj[2][1] *= -1.0f;

    return proj;
}

glm::vec2 VulkanSFRRenderer::getUVYRange(size_t gpuIndex)
{
    float stripHeight = 1.0f / static_cast<float>(devices.size());
    return {gpuIndex * stripHeight, (gpuIndex + 1) * stripHeight};
}

void VulkanSFRRenderer::recordSFRRenderCommands(size_t gpuIndex, VkCommandBuffer commandBuffer,
                                                uint32_t imageIndex, uint32_t yOffset, uint32_t renderHeight)
{
    auto viewport = getFrameViewport(gpuIndex);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = multiGpuRenderPasses[gpuIndex];
    renderPassInfo.framebuffer = sfrFramebuffers[gpuIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {static_cast<uint32_t>(viewport.width), static_cast<uint32_t>(viewport.height)};

    std::array<VkClearValue, 2> clearValues{};
    if (gpuIndex == 0)
        clearValues[0].color = {{0.15f, 0.05f, 0.05f, 1.0f}};
    else
        clearValues[0].color = {{0.05f, 0.05f, 0.15f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkRect2D scissor{};
    scissor.offset = {0, static_cast<int32_t>(yOffset)};
    scissor.extent = {RENDER_WIDTH, renderHeight};

    recordDrawCommands(commandBuffer, gpuIndex, imageIndex, viewport, scissor);

    vkCmdEndRenderPass(commandBuffer);

    currentScene->recordPostRenderPassCommands(commandBuffer, gpuIndex,
                                               sfrRenderImages[gpuIndex],
                                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                               static_cast<uint32_t>(viewport.width),
                                               static_cast<uint32_t>(viewport.height));
}

void VulkanSFRRenderer::drawFrame()
{
    if (devices.size() == 1)
    {
        drawFrameSingleGPU();
        return;
    }

    uint32_t sectionHeight = RENDER_HEIGHT / devices.size();
    VkDeviceSize sectionSize = RENDER_WIDTH * sectionHeight * 4;

    // PHASE 1: Wait for THIS frame's previous composite/present to complete
    auto fenceStart = std::chrono::high_resolution_clock::now();
    vkWaitForFences(devices[mainGPU], 1, &sfrCompositeFences[currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(devices[mainGPU], 1, &sfrCompositeFences[currentFrame]);

    vkWaitForFences(devices[mainGPU], 1, &inFlightFences[mainGPU][currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(devices[mainGPU], swapChain, UINT64_MAX,
                                            imageAvailableSemaphores[mainGPU][currentFrame],
                                            VK_NULL_HANDLE, &imageIndex);

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("Failed to acquire swap chain image!");
    }

    for (size_t i = 1; i < devices.size(); i++)
    {
        vkWaitForFences(devices[i], 1, &inFlightFences[i][currentFrame], VK_TRUE, UINT64_MAX);
    }

    if (benchmarkEnabled && benchmark)
    {
        double fenceWaitMs = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - fenceStart).count();
        benchmark->addFenceWaitTime(fenceWaitMs);
    }

    for (size_t i = 0; i < devices.size(); i++)
    {
        vkResetFences(devices[i], 1, &inFlightFences[i][currentFrame]);
    }

    // PHASE 2: Submit ALL GPUs' render work in parallel
    for (size_t i = 0; i < devices.size(); i++)
    {
        updateCameraUniformBuffer(i, imageIndex);
        updateSceneUniformBuffer(i, imageIndex);

        vkResetCommandBuffer(commandBuffers[i][imageIndex], 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(commandBuffers[i][imageIndex], &beginInfo);

        recordSFRRenderCommands(i, commandBuffers[i][imageIndex], imageIndex, 0, sectionHeight);

        // After render pass, sfrRenderImages[i] is in TRANSFER_SRC_OPTIMAL
        // Copy render result to the external memory image for cross-GPU transfer
        if (useExternalMemory && i != mainGPU)
        {
            transitionImageLayout(commandBuffers[i][imageIndex], sfrExternalImages[i].sourceImage,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageCopy copyRegion{};
            copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.srcSubresource.mipLevel = 0;
            copyRegion.srcSubresource.baseArrayLayer = 0;
            copyRegion.srcSubresource.layerCount = 1;
            copyRegion.srcOffset = {0, 0, 0};
            copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.dstSubresource.mipLevel = 0;
            copyRegion.dstSubresource.baseArrayLayer = 0;
            copyRegion.dstSubresource.layerCount = 1;
            copyRegion.dstOffset = {0, 0, 0};
            copyRegion.extent = {RENDER_WIDTH, sectionHeight, 1};

            vkCmdCopyImage(commandBuffers[i][imageIndex],
                           sfrRenderImages[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           sfrExternalImages[i].sourceImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyRegion);
        }

        if (!useExternalMemory)
        {
            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {RENDER_WIDTH, sectionHeight, 1};

            vkCmdCopyImageToBuffer(commandBuffers[i][imageIndex], sfrRenderImages[i],
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sfrStagingBuffers[i], 1, &region);
        }

        vkEndCommandBuffer(commandBuffers[i][imageIndex]);
    }

    for (size_t i = 0; i < devices.size(); i++)
    {
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[mainGPU][currentFrame]};

        if (i == 0)
        {
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = waitSemaphores;
            submitInfo.pWaitDstStageMask = waitStages;
        }

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[i][imageIndex];

        // Only signal semaphore for mainGPU (cross-GPU sync uses CPU fence wait)
        VkSemaphore signalSemaphore = VK_NULL_HANDLE;
        if (i == mainGPU)
        {
            signalSemaphore = sfrRenderCompleteSemaphores[i];
        }

        if (signalSemaphore != VK_NULL_HANDLE)
        {
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &signalSemaphore;
        }

        if (vkQueueSubmit(graphicsQueues[i], 1, &submitInfo, inFlightFences[i][currentFrame]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to submit GPU " + std::to_string(i) + " render commands!");
        }
    }

    // PHASE 3: Composite on main GPU
    if (useExternalMemory)
    {
        // Wait on CPU for all non-main GPUs to finish writing to host memory
        for (size_t i = 1; i < devices.size(); i++)
        {
            vkWaitForFences(devices[i], 1, &inFlightFences[i][currentFrame], VK_TRUE, UINT64_MAX);
        }

        VkCommandBuffer compositeCmd = sfrCompositeCommandBuffers[currentFrame];
        vkResetCommandBuffer(compositeCmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(compositeCmd, &beginInfo);

        transitionImageLayout(compositeCmd, swapChainImages[imageIndex],
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        for (size_t i = 0; i < devices.size(); i++)
        {
            uint32_t yOffset = i * sectionHeight;

            VkImage srcImage = (i == mainGPU) ? sfrRenderImages[mainGPU] : sfrExternalImages[i].importedImage;

            // mainGPU's render image is already TRANSFER_SRC_OPTIMAL from the render pass
            // imported external images need an explicit transition
            if (i != mainGPU)
            {
                transitionImageLayout(compositeCmd, srcImage,
                                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            }

            VkImageCopy copyRegion{};
            copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.srcSubresource.mipLevel = 0;
            copyRegion.srcSubresource.baseArrayLayer = 0;
            copyRegion.srcSubresource.layerCount = 1;
            copyRegion.srcOffset = {0, 0, 0};
            copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.dstSubresource.mipLevel = 0;
            copyRegion.dstSubresource.baseArrayLayer = 0;
            copyRegion.dstSubresource.layerCount = 1;
            copyRegion.dstOffset = {0, static_cast<int32_t>(yOffset), 0};
            copyRegion.extent = {RENDER_WIDTH, sectionHeight, 1};

            vkCmdCopyImage(compositeCmd,
                           srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyRegion);
        }

        transitionImageLayout(compositeCmd, swapChainImages[imageIndex],
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        vkEndCommandBuffer(compositeCmd);

        VkSubmitInfo compositeSubmit{};
        compositeSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        compositeSubmit.commandBufferCount = 1;
        compositeSubmit.pCommandBuffers = &compositeCmd;

        std::vector<VkSemaphore> waitSemaphores;
        std::vector<VkPipelineStageFlags> waitStages;

        // Only wait on mainGPU render semaphore (non-main GPUs already synced via CPU fence wait above)
        waitSemaphores.push_back(sfrRenderCompleteSemaphores[mainGPU]);
        waitStages.push_back(VK_PIPELINE_STAGE_TRANSFER_BIT);

        compositeSubmit.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
        compositeSubmit.pWaitSemaphores = waitSemaphores.data();
        compositeSubmit.pWaitDstStageMask = waitStages.data();

        VkSemaphore presentReadySemaphore = sfrPresentReadySemaphores[imageIndex];
        compositeSubmit.signalSemaphoreCount = 1;
        compositeSubmit.pSignalSemaphores = &presentReadySemaphore;

        if (vkQueueSubmit(graphicsQueues[mainGPU], 1, &compositeSubmit, sfrCompositeFences[currentFrame]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to submit composite commands!");
        }
    }
    else
    {
        auto stagingFenceStart = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < devices.size(); i++)
        {
            vkWaitForFences(devices[i], 1, &inFlightFences[i][currentFrame], VK_TRUE, UINT64_MAX);
        }
        if (benchmarkEnabled && benchmark)
        {
            double fenceMs = std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - stagingFenceStart).count();
            benchmark->addFenceWaitTime(fenceMs);
        }

        auto memcpyStart = std::chrono::high_resolution_clock::now();
        for (size_t i = 1; i < devices.size(); i++)
        {
            memcpy(static_cast<char*>(sfrStagingMapped[mainGPU]) + i * sectionSize,
                   sfrStagingMapped[i], sectionSize);
        }
        if (benchmarkEnabled && benchmark)
        {
            double memcpyMs = std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - memcpyStart).count();
            benchmark->addMemcpyTime(memcpyMs);
        }

        VkCommandBuffer compositeCmd = sfrCompositeCommandBuffers[currentFrame];
        vkResetCommandBuffer(compositeCmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(compositeCmd, &beginInfo);

        auto sfrCompositeImage = swapChainImages[imageIndex];

        transitionImageLayout(compositeCmd, sfrCompositeImage,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        for (size_t i = 0; i < devices.size(); i++)
        {
            uint32_t yOffset = i * sectionHeight;

            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, static_cast<int32_t>(yOffset), 0};
            region.imageExtent = {RENDER_WIDTH, sectionHeight, 1};

            vkCmdCopyBufferToImage(compositeCmd, sfrStagingBuffers[mainGPU],
                                   sfrCompositeImage,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        }

        transitionImageLayout(compositeCmd, sfrCompositeImage,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        vkEndCommandBuffer(compositeCmd);

        VkSubmitInfo compositeSubmit{};
        compositeSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        compositeSubmit.commandBufferCount = 1;
        compositeSubmit.pCommandBuffers = &compositeCmd;

        VkSemaphore waitSem = sfrRenderCompleteSemaphores[mainGPU];
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        compositeSubmit.waitSemaphoreCount = 1;
        compositeSubmit.pWaitSemaphores = &waitSem;
        compositeSubmit.pWaitDstStageMask = &waitStage;

        VkSemaphore presentReadySemaphore = sfrPresentReadySemaphores[imageIndex];
        compositeSubmit.signalSemaphoreCount = 1;
        compositeSubmit.pSignalSemaphores = &presentReadySemaphore;

        if (vkQueueSubmit(graphicsQueues[mainGPU], 1, &compositeSubmit, sfrCompositeFences[currentFrame]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to submit composite commands!");
        }
    }

    // PHASE 4: Present on dedicated present queue
    VkSemaphore presentReadySemaphore = sfrPresentReadySemaphores[imageIndex];

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &presentReadySemaphore;

    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueues[mainGPU], &presentInfo);

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("Failed to present swap chain image!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    frameNumber++;
}
