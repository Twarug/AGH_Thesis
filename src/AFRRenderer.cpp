#include "AFRRenderer.h"

void VulkanAFRRenderer::createMultiGpuRenderPasses()
{
    multiGpuRenderPasses.resize(devices.size());

    for (size_t i = 0; i < devices.size(); i++)
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapChainImageFormats[0];
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

void VulkanAFRRenderer::createAFRResources()
{
    createMultiGpuRenderPasses();

    useExternalMemory = externalMemorySupported && devices.size() > 1;

    if (useExternalMemory)
    {
        if (!checkExternalMemoryCompatible(1, 0, swapChainImageFormats[0]))
        {
            std::println("AFR: External memory not compatible between GPUs, using staging buffers");
            useExternalMemory = false;
        }
    }

    if (useExternalMemory)
    {
        std::println("AFR: Attempting external memory for zero-copy cross-GPU transfer...");
        if (!createExternalMemoryResources())
        {
            std::println("AFR: External memory import failed, falling back to staging buffers");
            useExternalMemory = false;
            createStagingBufferResources();
        }
    }
    else
    {
        std::println("AFR: Using staging buffers for cross-GPU transfer (CPU memcpy)");
        createStagingBufferResources();
    }

    size_t mainGPU = 0;
    createImage(mainGPU, RENDER_WIDTH, RENDER_HEIGHT, swapChainImageFormats[0],
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                afrCompositeImage, afrCompositeImageMemory);

    afrRenderCompleteSemaphores.resize(devices.size());
    for (size_t i = 0; i < devices.size(); i++)
    {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore(devices[i], &semaphoreInfo, nullptr, &afrRenderCompleteSemaphores[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create AFR render complete semaphore!");
        }
    }

    size_t swapchainImageCount = swapChainImages[mainGPU].size();
    afrPresentReadySemaphores.resize(swapchainImageCount);
    for (size_t i = 0; i < swapchainImageCount; i++)
    {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore(devices[mainGPU], &semaphoreInfo, nullptr, &afrPresentReadySemaphores[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create AFR present ready semaphore!");
        }
    }

    afrCompositeCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPools[mainGPU];
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateCommandBuffers(devices[mainGPU], &allocInfo, afrCompositeCommandBuffers.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate AFR composite command buffers!");
    }

    afrCompositeFences.resize(MAX_FRAMES_IN_FLIGHT);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(devices[mainGPU], &fenceInfo, nullptr, &afrCompositeFences[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create AFR composite fence!");
        }
    }

    std::println("Created AFR composite image and synchronization resources");
}

bool VulkanAFRRenderer::createExternalMemoryResources()
{
    size_t mainGPU = 0;

    afrRenderImages.resize(devices.size());
    afrRenderImageMemories.resize(devices.size());
    afrRenderImageViews.resize(devices.size());
    afrFramebuffers.resize(devices.size());

    afrExternalImages.resize(devices.size());
    afrExternalSemaphores.resize(devices.size());

    for (size_t i = 0; i < devices.size(); i++)
    {
        if (i == mainGPU)
        {
            createImage(i, RENDER_WIDTH, RENDER_HEIGHT, swapChainImageFormats[0],
                        VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        afrRenderImages[i], afrRenderImageMemories[i]);
        }
        else
        {
            createExportableImage(i, RENDER_WIDTH, RENDER_HEIGHT, swapChainImageFormats[0],
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                  afrRenderImages[i], afrRenderImageMemories[i]);

            afrExternalImages[i].sourceImage = afrRenderImages[i];
            afrExternalImages[i].sourceMemory = afrRenderImageMemories[i];
            afrExternalImages[i].sourceGpuIndex = i;
            afrExternalImages[i].format = swapChainImageFormats[0];
            afrExternalImages[i].width = RENDER_WIDTH;
            afrExternalImages[i].height = RENDER_HEIGHT;

            if (!importExternalImage(mainGPU, afrExternalImages[i]))
            {
                cleanupPartialExternalMemoryResources(i);
                return false;
            }

            createExportableSemaphore(i, afrExternalSemaphores[i].sourceSemaphore);
            afrExternalSemaphores[i].sourceGpuIndex = i;
            importExternalSemaphore(mainGPU, afrExternalSemaphores[i]);
        }

        afrRenderImageViews[i] = createImageView(devices[i], afrRenderImages[i],
                                                 swapChainImageFormats[0], VK_IMAGE_ASPECT_COLOR_BIT);

        std::array<VkImageView, 2> attachments = {
            afrRenderImageViews[i],
            depthImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = multiGpuRenderPasses[i];
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = RENDER_WIDTH;
        framebufferInfo.height = RENDER_HEIGHT;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(devices[i], &framebufferInfo, nullptr, &afrFramebuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create AFR framebuffer!");
        }

        std::println("Created AFR render target {} of {}x{}{}",
                     i, RENDER_WIDTH, RENDER_HEIGHT, (i != mainGPU ? " (external memory)" : ""));
    }

    std::println("AFR: Successfully using external memory for zero-copy cross-GPU transfer");
    return true;
}

void VulkanAFRRenderer::createStagingBufferResources()
{
    afrRenderImages.resize(devices.size());
    afrRenderImageMemories.resize(devices.size());
    afrRenderImageViews.resize(devices.size());
    afrFramebuffers.resize(devices.size());
    afrStagingBuffers.resize(devices.size());
    afrStagingMemories.resize(devices.size());
    afrStagingMapped.resize(devices.size());

    for (size_t i = 0; i < devices.size(); i++)
    {
        createImage(i, RENDER_WIDTH, RENDER_HEIGHT, swapChainImageFormats[0],
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    afrRenderImages[i], afrRenderImageMemories[i]);

        afrRenderImageViews[i] = createImageView(devices[i], afrRenderImages[i],
                                                 swapChainImageFormats[0], VK_IMAGE_ASPECT_COLOR_BIT);

        std::array<VkImageView, 2> attachments = {
            afrRenderImageViews[i],
            depthImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = multiGpuRenderPasses[i];
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = RENDER_WIDTH;
        framebufferInfo.height = RENDER_HEIGHT;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(devices[i], &framebufferInfo, nullptr, &afrFramebuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create AFR framebuffer!");
        }

        VkDeviceSize bufferSize = RENDER_WIDTH * RENDER_HEIGHT * 4;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(devices[i], &bufferInfo, nullptr, &afrStagingBuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create AFR staging buffer!");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(devices[i], afrStagingBuffers[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(i, memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(devices[i], &allocInfo, nullptr, &afrStagingMemories[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate AFR staging buffer memory!");
        }

        vkBindBufferMemory(devices[i], afrStagingBuffers[i], afrStagingMemories[i], 0);

        vkMapMemory(devices[i], afrStagingMemories[i], 0, bufferSize, 0, &afrStagingMapped[i]);

        std::println("Created AFR render target {} of {}x{}", i, RENDER_WIDTH, RENDER_HEIGHT);
    }
}

void VulkanAFRRenderer::cleanupPartialExternalMemoryResources(size_t failedIndex)
{
    for (size_t i = 0; i <= failedIndex && i < devices.size(); i++)
    {
        if (afrFramebuffers.size() > i && afrFramebuffers[i] != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(devices[i], afrFramebuffers[i], nullptr);
        }
        if (afrRenderImageViews.size() > i && afrRenderImageViews[i] != VK_NULL_HANDLE)
        {
            vkDestroyImageView(devices[i], afrRenderImageViews[i], nullptr);
        }
        if (afrRenderImages.size() > i && afrRenderImages[i] != VK_NULL_HANDLE)
        {
            vkDestroyImage(devices[i], afrRenderImages[i], nullptr);
        }
        if (afrRenderImageMemories.size() > i && afrRenderImageMemories[i] != VK_NULL_HANDLE)
        {
            vkFreeMemory(devices[i], afrRenderImageMemories[i], nullptr);
        }
    }

    for (auto& extSem : afrExternalSemaphores)
    {
        if (extSem.sourceSemaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(devices[extSem.sourceGpuIndex], extSem.sourceSemaphore, nullptr);
        }
    }

    afrFramebuffers.clear();
    afrRenderImageViews.clear();
    afrRenderImages.clear();
    afrRenderImageMemories.clear();
    afrExternalImages.clear();
    afrExternalSemaphores.clear();
}

void VulkanAFRRenderer::cleanupAFRResources()
{
    size_t mainGPU = 0;

    for (size_t i = 0; i < devices.size(); i++)
    {
        vkDeviceWaitIdle(devices[i]);
    }

    for (size_t i = 0; i < afrRenderCompleteSemaphores.size(); i++)
    {
        if (afrRenderCompleteSemaphores[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(devices[i], afrRenderCompleteSemaphores[i], nullptr);
        }
    }
    afrRenderCompleteSemaphores.clear();

    for (size_t i = 0; i < afrPresentReadySemaphores.size(); i++)
    {
        if (afrPresentReadySemaphores[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(devices[mainGPU], afrPresentReadySemaphores[i], nullptr);
        }
    }
    afrPresentReadySemaphores.clear();

    if (!afrCompositeCommandBuffers.empty())
    {
        vkFreeCommandBuffers(devices[mainGPU], commandPools[mainGPU],
                             static_cast<uint32_t>(afrCompositeCommandBuffers.size()),
                             afrCompositeCommandBuffers.data());
        afrCompositeCommandBuffers.clear();
    }

    for (size_t i = 0; i < afrCompositeFences.size(); i++)
    {
        if (afrCompositeFences[i] != VK_NULL_HANDLE)
        {
            vkDestroyFence(devices[mainGPU], afrCompositeFences[i], nullptr);
        }
    }
    afrCompositeFences.clear();

    for (auto& extSem : afrExternalSemaphores)
    {
        destroyExternalSemaphore(extSem);
    }
    afrExternalSemaphores.clear();

    for (size_t i = 0; i < afrStagingBuffers.size(); i++)
    {
        if (afrStagingMapped[i])
        {
            vkUnmapMemory(devices[i], afrStagingMemories[i]);
        }
        if (afrStagingBuffers[i] != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(devices[i], afrStagingBuffers[i], nullptr);
        }
        if (afrStagingMemories[i] != VK_NULL_HANDLE)
        {
            vkFreeMemory(devices[i], afrStagingMemories[i], nullptr);
        }
    }
    afrStagingMapped.clear();
    afrStagingMemories.clear();
    afrStagingBuffers.clear();

    for (size_t i = 0; i < afrFramebuffers.size(); i++)
    {
        if (afrFramebuffers[i] != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(devices[i], afrFramebuffers[i], nullptr);
        }
    }
    afrFramebuffers.clear();

    for (size_t i = 0; i < afrRenderImageViews.size(); i++)
    {
        if (afrRenderImageViews[i] != VK_NULL_HANDLE)
        {
            vkDestroyImageView(devices[i], afrRenderImageViews[i], nullptr);
        }
    }
    afrRenderImageViews.clear();

    for (size_t i = 1; i < afrExternalImages.size(); i++)
    {
        if (afrExternalImages[i].importedImage != VK_NULL_HANDLE)
        {
            vkDestroyImageView(devices[mainGPU], afrExternalImages[i].importedImageView, nullptr);
            vkDestroyImage(devices[mainGPU], afrExternalImages[i].importedImage, nullptr);
            vkFreeMemory(devices[mainGPU], afrExternalImages[i].importedMemory, nullptr);
#ifdef _WIN32
            if (afrExternalImages[i].sharedHandle != nullptr)
            {
                CloseHandle(afrExternalImages[i].sharedHandle);
            }
#endif
        }
    }
    afrExternalImages.clear();

    for (size_t i = 0; i < afrRenderImages.size(); i++)
    {
        if (afrRenderImages[i] != VK_NULL_HANDLE)
        {
            vkDestroyImage(devices[i], afrRenderImages[i], nullptr);
        }
        if (afrRenderImageMemories[i] != VK_NULL_HANDLE)
        {
            vkFreeMemory(devices[i], afrRenderImageMemories[i], nullptr);
        }
    }
    afrRenderImages.clear();
    afrRenderImageMemories.clear();

    if (afrCompositeImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(devices[mainGPU], afrCompositeImage, nullptr);
        vkFreeMemory(devices[mainGPU], afrCompositeImageMemory, nullptr);
        afrCompositeImage = VK_NULL_HANDLE;
        afrCompositeImageMemory = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < multiGpuRenderPasses.size(); i++)
    {
        if (multiGpuRenderPasses[i]) vkDestroyRenderPass(devices[i], multiGpuRenderPasses[i], nullptr);
    }
    multiGpuRenderPasses.clear();
}

void VulkanAFRRenderer::drawFrame()
{
    if (devices.size() == 1)
    {
        drawFrameSingleGPU();
        return;
    }

    size_t mainGPU = 0;
    size_t renderGPU = frameNumber % devices.size();

    size_t gpuFrameIndex = (frameNumber / devices.size()) % MAX_FRAMES_IN_FLIGHT;

    // PHASE 1: Wait for this frame's previous composite/present to complete
    auto fenceStart = std::chrono::high_resolution_clock::now();
    vkWaitForFences(devices[mainGPU], 1, &afrCompositeFences[currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(devices[mainGPU], 1, &afrCompositeFences[currentFrame]);

    vkWaitForFences(devices[renderGPU], 1, &inFlightFences[renderGPU][gpuFrameIndex], VK_TRUE, UINT64_MAX);
    vkResetFences(devices[renderGPU], 1, &inFlightFences[renderGPU][gpuFrameIndex]);

    if (benchmarkEnabled && benchmark)
    {
        double fenceWaitMs = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - fenceStart).count();
        benchmark->addFenceWaitTime(fenceWaitMs);
    }

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(devices[mainGPU], swapChains[mainGPU], UINT64_MAX,
                                            imageAvailableSemaphores[mainGPU][currentFrame],
                                            VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        return;
    }

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to acquire swap chain image!");
    }

    // PHASE 2: Update uniform buffers and render on selected GPU
    updateCameraUniformBuffer(renderGPU, imageIndex);
    updateSceneUniformBuffer(renderGPU, imageIndex);

    vkResetCommandBuffer(commandBuffers[renderGPU][imageIndex], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffers[renderGPU][imageIndex], &beginInfo);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = multiGpuRenderPasses[renderGPU];
    renderPassInfo.framebuffer = afrFramebuffers[renderGPU];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {RENDER_WIDTH, RENDER_HEIGHT};

    std::array<VkClearValue, 2> clearValues{};
    if (renderGPU == 0)
        clearValues[0].color = {{0.15f, 0.05f, 0.05f, 1.0f}};
    else
        clearValues[0].color = {{0.05f, 0.05f, 0.15f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffers[renderGPU][imageIndex], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(RENDER_WIDTH);
    viewport.height = static_cast<float>(RENDER_HEIGHT);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {RENDER_WIDTH, RENDER_HEIGHT};

    recordDrawCommands(commandBuffers[renderGPU][imageIndex], renderGPU, imageIndex, viewport, scissor);

    vkCmdEndRenderPass(commandBuffers[renderGPU][imageIndex]);

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
        region.imageExtent = {RENDER_WIDTH, RENDER_HEIGHT, 1};

        vkCmdCopyImageToBuffer(commandBuffers[renderGPU][imageIndex], afrRenderImages[renderGPU],
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, afrStagingBuffers[renderGPU], 1, &region);
    }

    vkEndCommandBuffer(commandBuffers[renderGPU][imageIndex]);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[renderGPU][imageIndex];

    VkSemaphore renderSignalSemaphore = VK_NULL_HANDLE;
    if (useExternalMemory && renderGPU != mainGPU)
    {
        renderSignalSemaphore = afrExternalSemaphores[renderGPU].sourceSemaphore;
    }
    else if (renderGPU == mainGPU)
    {
        renderSignalSemaphore = afrRenderCompleteSemaphores[mainGPU];
    }

    if (renderSignalSemaphore != VK_NULL_HANDLE)
    {
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderSignalSemaphore;
    }

    if (vkQueueSubmit(graphicsQueues[renderGPU], 1, &submitInfo, inFlightFences[renderGPU][gpuFrameIndex]) !=
        VK_SUCCESS)
    {
        throw std::runtime_error("Failed to submit render command buffer!");
    }

    // PHASE 3: Composite on main GPU
    vkResetCommandBuffer(afrCompositeCommandBuffers[currentFrame], 0);

    VkCommandBufferBeginInfo compositeBeginInfo{};
    compositeBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(afrCompositeCommandBuffers[currentFrame], &compositeBeginInfo);

    if (renderGPU != mainGPU)
    {
        if (useExternalMemory)
        {
            transitionImageLayout(afrCompositeCommandBuffers[currentFrame], afrExternalImages[renderGPU].importedImage,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            transitionImageLayout(afrCompositeCommandBuffers[currentFrame], afrCompositeImage,
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
            copyRegion.extent = {RENDER_WIDTH, RENDER_HEIGHT, 1};

            vkCmdCopyImage(afrCompositeCommandBuffers[currentFrame],
                           afrExternalImages[renderGPU].importedImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           afrCompositeImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyRegion);

            transitionImageLayout(afrCompositeCommandBuffers[currentFrame], afrCompositeImage,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        }
        else
        {
            VkDeviceSize frameSize = RENDER_WIDTH * RENDER_HEIGHT * 4;

            auto stagingFenceStart = std::chrono::high_resolution_clock::now();
            vkWaitForFences(devices[renderGPU], 1, &inFlightFences[renderGPU][gpuFrameIndex], VK_TRUE, UINT64_MAX);
            if (benchmarkEnabled && benchmark)
            {
                double fenceMs = std::chrono::duration<double, std::milli>(
                    std::chrono::high_resolution_clock::now() - stagingFenceStart).count();
                benchmark->addFenceWaitTime(fenceMs);
            }

            auto memcpyStart = std::chrono::high_resolution_clock::now();
            memcpy(afrStagingMapped[mainGPU], afrStagingMapped[renderGPU], frameSize);
            if (benchmarkEnabled && benchmark)
            {
                double memcpyMs = std::chrono::duration<double, std::milli>(
                    std::chrono::high_resolution_clock::now() - memcpyStart).count();
                benchmark->addMemcpyTime(memcpyMs);
            }

            transitionImageLayout(afrCompositeCommandBuffers[currentFrame], afrCompositeImage,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkBufferImageCopy copyRegion{};
            copyRegion.bufferOffset = 0;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;
            copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.imageSubresource.mipLevel = 0;
            copyRegion.imageSubresource.baseArrayLayer = 0;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageOffset = {0, 0, 0};
            copyRegion.imageExtent = {RENDER_WIDTH, RENDER_HEIGHT, 1};

            vkCmdCopyBufferToImage(afrCompositeCommandBuffers[currentFrame], afrStagingBuffers[mainGPU],
                                   afrCompositeImage,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            transitionImageLayout(afrCompositeCommandBuffers[currentFrame], afrCompositeImage,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        }
    }
    else
    {
        transitionImageLayout(afrCompositeCommandBuffers[currentFrame], afrCompositeImage,
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
        copyRegion.extent = {RENDER_WIDTH, RENDER_HEIGHT, 1};

        vkCmdCopyImage(afrCompositeCommandBuffers[currentFrame],
                       afrRenderImages[mainGPU], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       afrCompositeImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &copyRegion);

        transitionImageLayout(afrCompositeCommandBuffers[currentFrame], afrCompositeImage,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    }

    transitionImageLayout(afrCompositeCommandBuffers[currentFrame], swapChainImages[mainGPU][imageIndex],
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkImageBlit blitRegion{};
    blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.srcSubresource.mipLevel = 0;
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.srcOffsets[0] = {0, 0, 0};
    blitRegion.srcOffsets[1] = {static_cast<int32_t>(RENDER_WIDTH), static_cast<int32_t>(RENDER_HEIGHT), 1};
    blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.dstSubresource.mipLevel = 0;
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.dstOffsets[0] = {0, 0, 0};
    blitRegion.dstOffsets[1] = {
        static_cast<int32_t>(swapChainExtents[mainGPU].width),
        static_cast<int32_t>(swapChainExtents[mainGPU].height), 1
    };

    vkCmdBlitImage(afrCompositeCommandBuffers[currentFrame],
                   afrCompositeImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapChainImages[mainGPU][imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blitRegion, VK_FILTER_LINEAR);

    transitionImageLayout(afrCompositeCommandBuffers[currentFrame], swapChainImages[mainGPU][imageIndex],
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    vkEndCommandBuffer(afrCompositeCommandBuffers[currentFrame]);

    VkSubmitInfo compositeSubmit{};
    compositeSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    compositeSubmit.commandBufferCount = 1;
    compositeSubmit.pCommandBuffers = &afrCompositeCommandBuffers[currentFrame];

    std::vector<VkSemaphore> waitSemaphores;
    std::vector<VkPipelineStageFlags> waitStages;

    waitSemaphores.push_back(imageAvailableSemaphores[mainGPU][currentFrame]);
    waitStages.push_back(VK_PIPELINE_STAGE_TRANSFER_BIT);

    if (useExternalMemory && renderGPU != mainGPU)
    {
        waitSemaphores.push_back(afrExternalSemaphores[renderGPU].importedSemaphore);
        waitStages.push_back(VK_PIPELINE_STAGE_TRANSFER_BIT);
    }
    else if (renderGPU == mainGPU)
    {
        waitSemaphores.push_back(afrRenderCompleteSemaphores[mainGPU]);
        waitStages.push_back(VK_PIPELINE_STAGE_TRANSFER_BIT);
    }

    compositeSubmit.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
    compositeSubmit.pWaitSemaphores = waitSemaphores.data();
    compositeSubmit.pWaitDstStageMask = waitStages.data();

    compositeSubmit.signalSemaphoreCount = 1;
    compositeSubmit.pSignalSemaphores = &afrPresentReadySemaphores[imageIndex];

    vkQueueSubmit(graphicsQueues[mainGPU], 1, &compositeSubmit, afrCompositeFences[currentFrame]);

    // PHASE 4: Present on dedicated present queue
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &afrPresentReadySemaphores[imageIndex];

    VkSwapchainKHR swapChains_local[] = {swapChains[mainGPU]};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains_local;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueues[mainGPU], &presentInfo);

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("Failed to present swap chain image!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    frameNumber++;
}
