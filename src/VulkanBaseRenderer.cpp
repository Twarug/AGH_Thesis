#include "VulkanBaseRenderer.h"

constexpr bool spoofGPU = true;

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        std::println(stderr, "Validation layer: {}", pCallbackData->pMessage);
    }
    return VK_FALSE;
}

bool VulkanBaseRenderer::checkValidationLayerSupport()
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers)
    {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers)
        {
            if (strcmp(layerName, layerProperties.layerName) == 0)
            {
                layerFound = true;
                break;
            }
        }

        if (!layerFound)
        {
            return false;
        }
    }

    return true;
}

void VulkanBaseRenderer::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

void VulkanBaseRenderer::setupDebugMessenger()
{
    if (!enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);

    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));

    if (func == nullptr || func(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to set up debug messenger!");
    }
}

void VulkanBaseRenderer::createInstance()
{
    if (enableValidationLayers && !checkValidationLayerSupport())
    {
        throw std::runtime_error("Validation layers requested, but not available!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan Multi-GPU Renderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidationLayers)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    extensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
    extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = &debugCreateInfo;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create instance!");
    }

    if (enableValidationLayers)
    {
        std::println("Validation layers enabled");
    }
}

void VulkanBaseRenderer::createSurface()
{
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create window surface!");
    }
}

static uint64_t scorePhysicalDevice(VkPhysicalDevice device)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(device, &memProps);

    uint64_t score = 0;

    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        score += 10000000000ULL;
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        score += 1000000000ULL;

    for (uint32_t i = 0; i < memProps.memoryHeapCount; i++)
    {
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
        {
            score += memProps.memoryHeaps[i].size;
        }
    }

    score += props.limits.maxImageDimension2D;

    return score;
}

size_t VulkanBaseRenderer::countAvailableGPUs()
{
    if (!glfwInit())
    {
        return 0;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* tempWindow = glfwCreateWindow(1, 1, "", nullptr, nullptr);
    if (!tempWindow)
    {
        glfwTerminate();
        return 0;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "GPU Counter";
    appInfo.apiVersion = VK_API_VERSION_1_2;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkInstance tempInstance;
    if (vkCreateInstance(&createInfo, nullptr, &tempInstance) != VK_SUCCESS)
    {
        glfwDestroyWindow(tempWindow);
        glfwTerminate();
        return 0;
    }

    VkSurfaceKHR tempSurface;
    if (glfwCreateWindowSurface(tempInstance, tempWindow, nullptr, &tempSurface) != VK_SUCCESS)
    {
        vkDestroyInstance(tempInstance, nullptr);
        glfwDestroyWindow(tempWindow);
        glfwTerminate();
        return 0;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(tempInstance, &deviceCount, nullptr);

    size_t suitableCount = 0;
    if (deviceCount > 0)
    {
        std::vector<VkPhysicalDevice> allDevices(deviceCount);
        vkEnumeratePhysicalDevices(tempInstance, &deviceCount, allDevices.data());

        for (const auto& device : allDevices)
        {
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

            bool hasGraphics = false;
            bool hasPresent = false;
            for (uint32_t i = 0; i < queueFamilyCount; i++)
            {
                if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                    hasGraphics = true;
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, tempSurface, &presentSupport);
                if (presentSupport)
                    hasPresent = true;
            }

            if (hasGraphics && hasPresent)
                suitableCount++;
        }
    }

    vkDestroySurfaceKHR(tempInstance, tempSurface, nullptr);
    vkDestroyInstance(tempInstance, nullptr);
    glfwDestroyWindow(tempWindow);
    glfwTerminate();

    return suitableCount + (spoofGPU && suitableCount == 1 ? 1 : 0);
}

void VulkanBaseRenderer::pickPhysicalDevices()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> allDevices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, allDevices.data());

    std::vector<std::pair<uint64_t, VkPhysicalDevice>> scoredDevices;
    for (const auto& device : allDevices)
    {
        if (isDeviceSuitable(device))
        {
            uint64_t score = scorePhysicalDevice(device);
            scoredDevices.emplace_back(score, device);
        }
    }

    std::sort(scoredDevices.begin(), scoredDevices.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    if (forcedGpuIndex >= 0 && !spoofGPU)
    {
        if (static_cast<size_t>(forcedGpuIndex) >= scoredDevices.size())
        {
            throw std::runtime_error("Forced GPU index " + std::to_string(forcedGpuIndex) +
                " is out of range (only " + std::to_string(scoredDevices.size()) + " GPUs available)");
        }

        auto [score, device] = scoredDevices[forcedGpuIndex];
        physicalDevices.push_back(device);

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        std::println("Using GPU {}: {} (score: {})", forcedGpuIndex, props.deviceName, score);
    }
    else
    {
        for (const auto& [score, device] : scoredDevices)
        {
            physicalDevices.push_back(device);

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            std::println("Selected GPU {}: {} (score: {})", physicalDevices.size(), props.deviceName, score);

            if (physicalDevices.size() >= 2) break;
        }
    }

    if (spoofGPU && physicalDevices.size() == 1)
    {
        physicalDevices.push_back(physicalDevices[0]);
        std::println("Spoofing GPU");
    }

    if (physicalDevices.empty())
    {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }

    if (forcedGpuIndex < 0)
    {
        std::println("Found {} suitable GPU(s), most powerful is primary", physicalDevices.size());
    }
    mainGPU = 0;
}

void VulkanBaseRenderer::createLogicalDevices()
{
    devices.resize(physicalDevices.size());
    graphicsQueues.resize(physicalDevices.size());
    presentQueues.resize(physicalDevices.size());

    for (size_t i = 0; i < physicalDevices.size(); i++)
    {
        QueueFamilyIndices indices = findQueueFamilies(physicalDevices[i]);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies)
        {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.multiDrawIndirect = VK_TRUE;

        VkPhysicalDeviceVulkan11Features vulkan11Features{};
        vulkan11Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        vulkan11Features.shaderDrawParameters = VK_TRUE;

        std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        if (physicalDevices.size() > 1 && checkExternalMemorySupport(physicalDevices[i]))
        {
            deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
            deviceExtensions.push_back(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
#ifdef _WIN32
            deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
#endif
            if (i == 0)
            {
                externalMemorySupported = true;
                std::println("External memory extensions enabled for multi-GPU zero-copy transfer");
            }
        }

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &vulkan11Features;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();
        createInfo.enabledLayerCount = 0;

        if (vkCreateDevice(physicalDevices[i], &createInfo, nullptr, &devices[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create logical device!");
        }

        vkGetDeviceQueue(devices[i], indices.graphicsFamily.value(), 0, &graphicsQueues[i]);
        vkGetDeviceQueue(devices[i], indices.presentFamily.value(), 0, &presentQueues[i]);
    }
}

void VulkanBaseRenderer::createSwapChains()
{
    swapChainImages.resize(physicalDevices.size());

    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevices[mainGPU]);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
    {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    QueueFamilyIndices indices = findQueueFamilies(physicalDevices[mainGPU]);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(devices[mainGPU], &createInfo, nullptr, &swapChain) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(devices[mainGPU], swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(devices[mainGPU], swapChain, &imageCount, swapChainImages.data());

    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;
}

void VulkanBaseRenderer::createImageViews()
{
    swapChainImageViews.resize(swapChainImages.size());

    for (size_t j = 0; j < swapChainImages.size(); j++)
    {
        swapChainImageViews[j] = createImageView(devices[mainGPU], swapChainImages[j],
                                                          swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void VulkanBaseRenderer::createRenderPasses()
{
    renderPasses.resize(devices.size());

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
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

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

        std::array attachments = {colorAttachment, depthAttachment};
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(devices[i], &renderPassInfo, nullptr, &renderPasses[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create render pass!");
        }
    }
}

void VulkanBaseRenderer::createDescriptorSetLayouts()
{
    cameraDescriptorSetLayouts.resize(devices.size());
    sceneDescriptorSetLayouts.resize(devices.size());

    for (size_t i = 0; i < devices.size(); i++)
    {
        VkDescriptorSetLayoutBinding cameraUboBinding{};
        cameraUboBinding.binding = 0;
        cameraUboBinding.descriptorCount = 1;
        cameraUboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        cameraUboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        cameraUboBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo cameraLayoutInfo{};
        cameraLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        cameraLayoutInfo.bindingCount = 1;
        cameraLayoutInfo.pBindings = &cameraUboBinding;

        if (vkCreateDescriptorSetLayout(devices[i], &cameraLayoutInfo, nullptr, &cameraDescriptorSetLayouts[i]) !=
            VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create camera descriptor set layout!");
        }

        currentScene->createDescriptorSetLayout(this, i, sceneDescriptorSetLayouts[i]);
    }
}

void VulkanBaseRenderer::createGraphicsPipelines()
{
    graphicsPipelines.resize(devices.size());
    pipelineLayouts.resize(devices.size());

    auto vertShaderCode = FileUtils::readBinaryFile(currentScene->getVertexShaderPath());
    auto fragShaderCode = FileUtils::readBinaryFile(currentScene->getFragmentShaderPath());

    for (size_t i = 0; i < devices.size(); i++)
    {
        VkShaderModule vertShaderModule = createShaderModule(i, vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(i, fragShaderCode);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        if (currentScene->needsVertexInput())
        {
            vertexInputInfo.vertexBindingDescriptionCount = 1;
            vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
            vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
            vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
        }
        else
        {
            vertexInputInfo.vertexBindingDescriptionCount = 0;
            vertexInputInfo.vertexAttributeDescriptionCount = 0;
        }

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        std::vector dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = currentScene->needsCulling() ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = currentScene->needsDepthTest() ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = currentScene->needsDepthTest() ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f;
        colorBlending.blendConstants[1] = 0.0f;
        colorBlending.blendConstants[2] = 0.0f;
        colorBlending.blendConstants[3] = 0.0f;

        std::array<VkDescriptorSetLayout, 2> setLayouts = {
            cameraDescriptorSetLayouts[i],
            sceneDescriptorSetLayouts[i]
        };

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        pipelineLayoutInfo.pSetLayouts = setLayouts.data();

        VkPushConstantRange* pushConstantRange = currentScene->getPushConstantRange();
        if (pushConstantRange)
        {
            pipelineLayoutInfo.pushConstantRangeCount = 1;
            pipelineLayoutInfo.pPushConstantRanges = pushConstantRange;
        }

        if (vkCreatePipelineLayout(devices[i], &pipelineLayoutInfo, nullptr, &pipelineLayouts[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create pipeline layout!");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayouts[i];
        pipelineInfo.renderPass = renderPasses[i];
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        if (vkCreateGraphicsPipelines(devices[i], VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipelines[i]) !=
            VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create graphics pipeline!");
        }

        vkDestroyShaderModule(devices[i], fragShaderModule, nullptr);
        vkDestroyShaderModule(devices[i], vertShaderModule, nullptr);
    }
}

void VulkanBaseRenderer::createFramebuffers()
{
    swapChainFramebuffers.resize(swapChainImageViews.size());

    for (size_t j = 0; j < swapChainImageViews.size(); j++)
    {
        std::array attachments = {
            swapChainImageViews[j],
            depthImageViews[mainGPU]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPasses[mainGPU];
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapChainExtent.width;
        framebufferInfo.height = swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(devices[mainGPU], &framebufferInfo, nullptr, &swapChainFramebuffers[j]) !=
            VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create framebuffer!");
        }
    }
}

void VulkanBaseRenderer::createCommandPools()
{
    commandPools.resize(devices.size());

    for (size_t i = 0; i < devices.size(); i++)
    {
        QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevices[i]);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(devices[i], &poolInfo, nullptr, &commandPools[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create command pool!");
        }
    }
}

void VulkanBaseRenderer::createDepthResources()
{
    depthImages.resize(devices.size());
    depthImageMemories.resize(devices.size());
    depthImageViews.resize(devices.size());

    for (size_t i = 0; i < devices.size(); i++)
    {
        VkFormat depthFormat = findDepthFormat(i);

        VkViewport viewport = getFrameViewport(i);

        createImage(i, viewport.width, viewport.height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    depthImages[i], depthImageMemories[i]);
        depthImageViews[i] = createImageView(devices[i], depthImages[i], depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
}

void VulkanBaseRenderer::createCameraUniformBuffers()
{
    cameraUniformBuffers.resize(devices.size());
    cameraUniformBuffersMemory.resize(devices.size());

    size_t imageCount = swapChainImages.size();

    VkDeviceSize bufferSize = sizeof(CameraUBO);

    for (size_t i = 0; i < devices.size(); i++)
    {
        cameraUniformBuffers[i].resize(imageCount);
        cameraUniformBuffersMemory[i].resize(imageCount);

        for (size_t j = 0; j < imageCount; j++)
        {
            createBuffer(i, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         cameraUniformBuffers[i][j], cameraUniformBuffersMemory[i][j]);
        }
    }
}

void VulkanBaseRenderer::createSceneUniformBuffers()
{
    sceneUniformBuffers.resize(devices.size());
    sceneUniformBuffersMemory.resize(devices.size());

    size_t imageCount = swapChainImages.size();

    for (size_t i = 0; i < devices.size(); i++)
    {
        currentScene->createUniformBuffers(this, i, imageCount,
                                           sceneUniformBuffers[i], sceneUniformBuffersMemory[i]);
    }
}

void VulkanBaseRenderer::createDescriptorPools()
{
    cameraDescriptorPools.resize(devices.size());
    sceneDescriptorPools.resize(devices.size());

    size_t imageCount = swapChainImages.size();

    for (size_t i = 0; i < devices.size(); i++)
    {
        VkDescriptorPoolSize cameraPoolSize{};
        cameraPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        cameraPoolSize.descriptorCount = static_cast<uint32_t>(imageCount);

        VkDescriptorPoolCreateInfo cameraPoolInfo{};
        cameraPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        cameraPoolInfo.poolSizeCount = 1;
        cameraPoolInfo.pPoolSizes = &cameraPoolSize;
        cameraPoolInfo.maxSets = static_cast<uint32_t>(imageCount);

        if (vkCreateDescriptorPool(devices[i], &cameraPoolInfo, nullptr, &cameraDescriptorPools[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create camera descriptor pool!");
        }

        currentScene->createDescriptorPool(this, i, imageCount, sceneDescriptorPools[i]);
    }
}

void VulkanBaseRenderer::createDescriptorSets()
{
    cameraDescriptorSets.resize(devices.size());
    sceneDescriptorSets.resize(devices.size());

    size_t imageCount = swapChainImages.size();

    for (size_t i = 0; i < devices.size(); i++)
    {
        std::vector<VkDescriptorSetLayout> cameraLayouts(imageCount, cameraDescriptorSetLayouts[i]);

        VkDescriptorSetAllocateInfo cameraAllocInfo{};
        cameraAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        cameraAllocInfo.descriptorPool = cameraDescriptorPools[i];
        cameraAllocInfo.descriptorSetCount = static_cast<uint32_t>(imageCount);
        cameraAllocInfo.pSetLayouts = cameraLayouts.data();

        cameraDescriptorSets[i].resize(imageCount);
        if (vkAllocateDescriptorSets(devices[i], &cameraAllocInfo, cameraDescriptorSets[i].data()) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate camera descriptor sets!");
        }

        for (size_t j = 0; j < imageCount; j++)
        {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = cameraUniformBuffers[i][j];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(CameraUBO);

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = cameraDescriptorSets[i][j];
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(devices[i], 1, &descriptorWrite, 0, nullptr);
        }

        currentScene->createDescriptorSets(this, i, imageCount,
                                           sceneDescriptorSetLayouts[i], sceneDescriptorPools[i],
                                           sceneUniformBuffers[i], sceneDescriptorSets[i]);
    }
}

void VulkanBaseRenderer::createCommandBuffers()
{
    commandBuffers.resize(devices.size());

    size_t bufferCount = swapChainImages.size();

    for (size_t i = 0; i < devices.size(); i++)
    {
        commandBuffers[i].resize(bufferCount);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPools[i];
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)commandBuffers[i].size();

        if (vkAllocateCommandBuffers(devices[i], &allocInfo, commandBuffers[i].data()) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate command buffers!");
        }
    }
}

void VulkanBaseRenderer::createSyncObjects()
{
    imageAvailableSemaphores.resize(devices.size());
    renderFinishedSemaphores.resize(devices.size());
    inFlightFences.resize(devices.size());

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    size_t swapChainImageCount = swapChainImages.size();

    for (size_t i = 0; i < devices.size(); i++)
    {
        imageAvailableSemaphores[i].resize(MAX_FRAMES_IN_FLIGHT);
        renderFinishedSemaphores[i].resize(swapChainImageCount);
        inFlightFences[i].resize(MAX_FRAMES_IN_FLIGHT);

        for (size_t j = 0; j < MAX_FRAMES_IN_FLIGHT; j++)
        {
            if (vkCreateSemaphore(devices[i], &semaphoreInfo, nullptr, &imageAvailableSemaphores[i][j]) != VK_SUCCESS ||
                vkCreateFence(devices[i], &fenceInfo, nullptr, &inFlightFences[i][j]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create synchronization objects!");
            }
        }

        for (size_t j = 0; j < swapChainImageCount; j++)
        {
            if (vkCreateSemaphore(devices[i], &semaphoreInfo, nullptr, &renderFinishedSemaphores[i][j]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create render finished semaphores!");
            }
        }
    }
}

glm::mat4 VulkanBaseRenderer::getViewMatrix(size_t gpuIndex)
{
    return glm::lookAt(glm::vec3(50.0f, 50.0f, 50.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::mat4 VulkanBaseRenderer::getProjectionMatrix(size_t gpuIndex)
{
    glm::mat4 proj = glm::perspective(
        glm::radians(45.0f),
        RENDER_WIDTH / static_cast<float>(RENDER_HEIGHT),
        0.1f, 500.0f);
    proj[1][1] *= -1;
    return proj;
}

glm::mat4 VulkanBaseRenderer::getModelMatrix(size_t gpuIndex)
{
    return glm::rotate(glm::mat4(1.0f), getTime() * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
}

float VulkanBaseRenderer::getTime() const
{
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
}

void VulkanBaseRenderer::updateCameraUniformBuffer(size_t gpuIndex, uint32_t currentImage)
{
    CameraUBO ubo{};
    ubo.view = getViewMatrix(gpuIndex);
    ubo.proj = getProjectionMatrix(gpuIndex);
    ubo.time = getTime();

    void* data;
    vkMapMemory(devices[gpuIndex], cameraUniformBuffersMemory[gpuIndex][currentImage], 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(devices[gpuIndex], cameraUniformBuffersMemory[gpuIndex][currentImage]);
}

void VulkanBaseRenderer::updateSceneUniformBuffer(size_t gpuIndex, uint32_t currentImage)
{
    if (!sceneUniformBuffersMemory[gpuIndex].empty())
    {
        currentScene->updateUniformBuffer(this, gpuIndex, currentImage,
                                          sceneUniformBuffersMemory[gpuIndex][currentImage]);
    }
}

void VulkanBaseRenderer::drawFrameSingleGPU()
{
    constexpr size_t gpu = 0;

    auto fenceStart = std::chrono::high_resolution_clock::now();
    vkWaitForFences(devices[gpu], 1, &inFlightFences[gpu][currentFrame], VK_TRUE, UINT64_MAX);
    if (benchmarkEnabled && benchmark)
    {
        double fenceWaitMs = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - fenceStart).count();
        benchmark->addFenceWaitTime(fenceWaitMs);
    }

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(devices[gpu], swapChain, UINT64_MAX,
                                            imageAvailableSemaphores[gpu][currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        return;
    }

    updateCameraUniformBuffer(gpu, imageIndex);
    updateSceneUniformBuffer(gpu, imageIndex);

    vkResetFences(devices[gpu], 1, &inFlightFences[gpu][currentFrame]);

    vkResetCommandBuffer(commandBuffers[gpu][imageIndex], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffers[gpu][imageIndex], &beginInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPasses[gpu];
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffers[gpu][imageIndex], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapChainExtent.width);
    viewport.height = static_cast<float>(swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChainExtent;

    recordDrawCommands(commandBuffers[gpu][imageIndex], gpu, imageIndex, viewport, scissor);

    vkCmdEndRenderPass(commandBuffers[gpu][imageIndex]);

    if (vkEndCommandBuffer(commandBuffers[gpu][imageIndex]) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to record command buffer!");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[gpu][currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[gpu][imageIndex];

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[gpu][imageIndex]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueues[gpu], 1, &submitInfo, inFlightFences[gpu][currentFrame]) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(presentQueues[gpu], &presentInfo);

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    frameNumber++;
}

void VulkanBaseRenderer::recordDrawCommands(VkCommandBuffer commandBuffer, size_t gpuIndex, uint32_t imageIndex,
                                            const VkViewport& viewport, const VkRect2D& scissor)
{
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelines[gpuIndex]);

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayouts[gpuIndex], 0, 1,
                            &cameraDescriptorSets[gpuIndex][imageIndex], 0, nullptr);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayouts[gpuIndex], 1, 1,
                            &sceneDescriptorSets[gpuIndex][imageIndex], 0, nullptr);

    currentScene->pushConstants(commandBuffer, pipelineLayouts[gpuIndex]);

    currentScene->recordDrawCommands(commandBuffer, gpuIndex);
}

void VulkanBaseRenderer::cleanup()
{
    if (currentScene)
    {
        currentScene->destroyBuffers(this);
    }

    for (size_t i = 0; i < devices.size(); i++)
    {
        for (size_t j = 0; j < MAX_FRAMES_IN_FLIGHT; j++)
        {
            vkDestroySemaphore(devices[i], imageAvailableSemaphores[i][j], nullptr);
            vkDestroyFence(devices[i], inFlightFences[i][j], nullptr);
        }

        for (size_t j = 0; j < renderFinishedSemaphores[i].size(); j++)
        {
            vkDestroySemaphore(devices[i], renderFinishedSemaphores[i][j], nullptr);
        }

        // Destroy camera descriptor resources
        vkDestroyDescriptorPool(devices[i], cameraDescriptorPools[i], nullptr);
        for (size_t j = 0; j < cameraUniformBuffers[i].size(); j++)
        {
            vkDestroyBuffer(devices[i], cameraUniformBuffers[i][j], nullptr);
            vkFreeMemory(devices[i], cameraUniformBuffersMemory[i][j], nullptr);
        }

        // Destroy scene descriptor resources
        vkDestroyDescriptorPool(devices[i], sceneDescriptorPools[i], nullptr);
        for (size_t j = 0; j < sceneUniformBuffers[i].size(); j++)
        {
            vkDestroyBuffer(devices[i], sceneUniformBuffers[i][j], nullptr);
            vkFreeMemory(devices[i], sceneUniformBuffersMemory[i][j], nullptr);
        }

        if (i == 0 && !swapChainFramebuffers.empty())
        {
            for (auto framebuffer : swapChainFramebuffers)
            {
                vkDestroyFramebuffer(devices[i], framebuffer, nullptr);
            }
        }

        vkDestroyImageView(devices[i], depthImageViews[i], nullptr);
        vkDestroyImage(devices[i], depthImages[i], nullptr);
        vkFreeMemory(devices[i], depthImageMemories[i], nullptr);

        vkDestroyCommandPool(devices[i], commandPools[i], nullptr);

        vkDestroyPipeline(devices[i], graphicsPipelines[i], nullptr);
        vkDestroyPipelineLayout(devices[i], pipelineLayouts[i], nullptr);
        vkDestroyDescriptorSetLayout(devices[i], cameraDescriptorSetLayouts[i], nullptr);
        vkDestroyDescriptorSetLayout(devices[i], sceneDescriptorSetLayouts[i], nullptr);
        vkDestroyRenderPass(devices[i], renderPasses[i], nullptr);

        if (i == 0 && !swapChainImageViews.empty())
        {
            for (auto imageView : swapChainImageViews)
            {
                vkDestroyImageView(devices[i], imageView, nullptr);
            }
        }

        if (i == 0 && swapChain)
        {
            vkDestroySwapchainKHR(devices[i], swapChain, nullptr);
        }

        vkDestroyDevice(devices[i], nullptr);
    }

    vkDestroySurfaceKHR(instance, surface, nullptr);

    if (enableValidationLayers)
    {
        auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (func != nullptr)
        {
            func(instance, debugMessenger, nullptr);
        }
    }

    vkDestroyInstance(instance, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();
}

bool VulkanBaseRenderer::isDeviceSuitable(VkPhysicalDevice device)
{
    auto indices = findQueueFamilies(device);
    bool extensionsSupported = checkDeviceExtensionSupport(device);
    bool swapChainAdequate = false;
    if (extensionsSupported)
    {
        auto swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }
    return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

bool VulkanBaseRenderer::checkDeviceExtensionSupport(VkPhysicalDevice device)
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto& extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

QueueFamilyIndices VulkanBaseRenderer::findQueueFamilies(VkPhysicalDevice device)
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies)
    {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

        if (presentSupport)
        {
            indices.presentFamily = i;
        }

        if (indices.isComplete())
        {
            break;
        }

        i++;
    }

    return indices;
}

SwapChainSupportDetails VulkanBaseRenderer::querySwapChainSupport(VkPhysicalDevice device)
{
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if (formatCount != 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR VulkanBaseRenderer::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
    for (const auto& availableFormat : availableFormats)
    {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace ==
            VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR VulkanBaseRenderer::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
{
    for (const auto& availablePresentMode : availablePresentModes)
    {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanBaseRenderer::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
{
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        return capabilities.currentExtent;
    }

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actualExtent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };

    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width,
                                    capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height,
                                     capabilities.maxImageExtent.height);

    return actualExtent;
}

VkShaderModule VulkanBaseRenderer::createShaderModule(size_t gpuIndex, const std::vector<char>& code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(devices[gpuIndex], &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create shader module!");
    }

    return shaderModule;
}

void VulkanBaseRenderer::createBuffer(size_t deviceIdx, VkDeviceSize size, VkBufferUsageFlags usage,
                                      VkMemoryPropertyFlags properties,
                                      VkBuffer& buffer, VkDeviceMemory& bufferMemory)
{
    if (size == 0)
    {
        throw std::runtime_error("Cannot create buffer with size 0!");
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(devices[deviceIdx], &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(devices[deviceIdx], buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(deviceIdx, memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(devices[deviceIdx], &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate buffer memory!");
    }

    vkBindBufferMemory(devices[deviceIdx], buffer, bufferMemory, 0);
}

void VulkanBaseRenderer::copyBuffer(size_t deviceIdx, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPools[deviceIdx];
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(devices[deviceIdx], &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueues[deviceIdx], 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueues[deviceIdx]);

    vkFreeCommandBuffers(devices[deviceIdx], commandPools[deviceIdx], 1, &commandBuffer);
}

uint32_t VulkanBaseRenderer::findMemoryType(size_t deviceIdx, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevices[deviceIdx], &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type!");
}

void VulkanBaseRenderer::createImage(size_t deviceIdx, uint32_t width, uint32_t height, VkFormat format,
                                     VkImageTiling tiling,
                                     VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image,
                                     VkDeviceMemory& imageMemory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(devices[deviceIdx], &imageInfo, nullptr, &image) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(devices[deviceIdx], image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(deviceIdx, memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(devices[deviceIdx], &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate image memory!");
    }

    vkBindImageMemory(devices[deviceIdx], image, imageMemory, 0);
}

VkImageView VulkanBaseRenderer::createImageView(VkDevice device, VkImage image, VkFormat format,
                                                VkImageAspectFlags aspectFlags)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create texture image view!");
    }

    return imageView;
}

VkFormat VulkanBaseRenderer::findDepthFormat(size_t gpuIndex)
{
    return findSupportedFormat(
        gpuIndex,
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

VkFormat VulkanBaseRenderer::findSupportedFormat(size_t gpuIndex,
                                                 const std::vector<VkFormat>& candidates, VkImageTiling tiling,
                                                 VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevices[gpuIndex], format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
        {
            return format;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
        {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported format!");
}


VkCommandBuffer VulkanBaseRenderer::beginSingleTimeCommands(size_t gpuIndex)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPools[gpuIndex];
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(devices[gpuIndex], &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void VulkanBaseRenderer::endSingleTimeCommands(size_t gpuIndex, VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueues[gpuIndex], 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueues[gpuIndex]);

    vkFreeCommandBuffers(devices[gpuIndex], commandPools[gpuIndex], 1, &commandBuffer);
}

void VulkanBaseRenderer::transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image,
                                               VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else
    {
        throw std::invalid_argument("Unsupported layout transition!");
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
}

bool VulkanBaseRenderer::checkExternalMemorySupport(VkPhysicalDevice device)
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions = {
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
#ifdef _WIN32
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME
#endif
    };

    for (const auto& extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

bool VulkanBaseRenderer::checkExternalMemoryCompatible(size_t sourceGpuIndex, size_t targetGpuIndex, VkFormat format)
{
    VkPhysicalDeviceExternalImageFormatInfo externalFormatInfo{};
    externalFormatInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    externalFormatInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;

    VkPhysicalDeviceImageFormatInfo2 formatInfo{};
    formatInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    formatInfo.pNext = &externalFormatInfo;
    formatInfo.format = format;
    formatInfo.type = VK_IMAGE_TYPE_2D;
    formatInfo.tiling = VK_IMAGE_TILING_LINEAR;
    formatInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkExternalImageFormatProperties externalProps{};
    externalProps.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;

    VkImageFormatProperties2 formatProps{};
    formatProps.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    formatProps.pNext = &externalProps;

    VkResult result = vkGetPhysicalDeviceImageFormatProperties2(
        physicalDevices[sourceGpuIndex], &formatInfo, &formatProps);

    if (result != VK_SUCCESS)
    {
        std::println("External memory: Source GPU {} doesn't support host allocation for this format", sourceGpuIndex);
        return false;
    }

    VkExternalMemoryFeatureFlags sourceFeatures =
        externalProps.externalMemoryProperties.externalMemoryFeatures;

    if (!(sourceFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
    {
        std::println("External memory: Source GPU doesn't support IMPORTABLE for host allocation");
        return false;
    }

    formatInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    result = vkGetPhysicalDeviceImageFormatProperties2(
        physicalDevices[targetGpuIndex], &formatInfo, &formatProps);

    if (result != VK_SUCCESS)
    {
        std::println("External memory: Target GPU {} doesn't support host allocation for this format", targetGpuIndex);
        return false;
    }

    VkExternalMemoryFeatureFlags targetFeatures =
        externalProps.externalMemoryProperties.externalMemoryFeatures;

    if (!(targetFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
    {
        std::println("External memory: Target GPU doesn't support IMPORTABLE for host allocation");
        return false;
    }

    return true;
}

void VulkanBaseRenderer::createExportableImage(size_t sourceGpuIndex, uint32_t width, uint32_t height,
                                               VkFormat format, VkImageUsageFlags usage,
                                               VkImage& image, VkDeviceMemory& memory,
                                               void*& hostPointer, size_t& allocationSize)
{
    VkExternalMemoryImageCreateInfo externalMemoryInfo{};
    externalMemoryInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    externalMemoryInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext = &externalMemoryInfo;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(devices[sourceGpuIndex], &imageInfo, nullptr, &image) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create exportable image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(devices[sourceGpuIndex], image, &memRequirements);

    // Query minimum host pointer alignment for this device
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT hostMemProps{};
    hostMemProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT;
    VkPhysicalDeviceProperties2 deviceProps2{};
    deviceProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProps2.pNext = &hostMemProps;
    vkGetPhysicalDeviceProperties2(physicalDevices[sourceGpuIndex], &deviceProps2);

    VkDeviceSize alignment = std::max(memRequirements.alignment, hostMemProps.minImportedHostPointerAlignment);
    VkDeviceSize alignedSize = (memRequirements.size + alignment - 1) & ~(alignment - 1);

    // Allocate aligned host memory
    void* hostPtr =
#ifdef _WIN32
        _aligned_malloc(static_cast<size_t>(alignedSize), static_cast<size_t>(alignment));
#else
        aligned_alloc(static_cast<size_t>(alignment), static_cast<size_t>(alignedSize));
#endif
    if (!hostPtr)
    {
        vkDestroyImage(devices[sourceGpuIndex], image, nullptr);
        image = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to allocate aligned host memory!");
    }

    // Query which memory types are compatible with this host pointer
    auto vkGetMemoryHostPointerPropertiesEXT = reinterpret_cast<PFN_vkGetMemoryHostPointerPropertiesEXT>(
        vkGetDeviceProcAddr(devices[sourceGpuIndex], "vkGetMemoryHostPointerPropertiesEXT"));

    if (!vkGetMemoryHostPointerPropertiesEXT)
    {
#ifdef _WIN32
        _aligned_free(hostPtr);
#else
        free(hostPtr);
#endif
        vkDestroyImage(devices[sourceGpuIndex], image, nullptr);
        image = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to get vkGetMemoryHostPointerPropertiesEXT function!");
    }

    VkMemoryHostPointerPropertiesEXT hostPointerProps{};
    hostPointerProps.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT;
    if (vkGetMemoryHostPointerPropertiesEXT(devices[sourceGpuIndex],
                                            VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
                                            hostPtr, &hostPointerProps) != VK_SUCCESS)
    {
#ifdef _WIN32
        _aligned_free(hostPtr);
#else
        free(hostPtr);
#endif
        vkDestroyImage(devices[sourceGpuIndex], image, nullptr);
        image = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to get host pointer memory properties!");
    }

    VkImportMemoryHostPointerInfoEXT importHostPtrInfo{};
    importHostPtrInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT;
    importHostPtrInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    importHostPtrInfo.pHostPointer = hostPtr;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &importHostPtrInfo;
    allocInfo.allocationSize = alignedSize;
    allocInfo.memoryTypeIndex = findMemoryType(sourceGpuIndex,
                                               memRequirements.memoryTypeBits & hostPointerProps.memoryTypeBits,
                                               0);

    if (vkAllocateMemory(devices[sourceGpuIndex], &allocInfo, nullptr, &memory) != VK_SUCCESS)
    {
#ifdef _WIN32
        _aligned_free(hostPtr);
#else
        free(hostPtr);
#endif
        vkDestroyImage(devices[sourceGpuIndex], image, nullptr);
        image = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to allocate exportable image memory!");
    }

    vkBindImageMemory(devices[sourceGpuIndex], image, memory, 0);

    hostPointer = hostPtr;
    allocationSize = static_cast<size_t>(alignedSize);
}

#ifdef _WIN32
HANDLE VulkanBaseRenderer::getSemaphoreWin32Handle(size_t gpuIndex, VkSemaphore semaphore)
{
    VkSemaphoreGetWin32HandleInfoKHR handleInfo{};
    handleInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
    handleInfo.semaphore = semaphore;
    handleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    HANDLE handle = nullptr;
    auto vkGetSemaphoreWin32HandleKHR = reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(
        vkGetDeviceProcAddr(devices[gpuIndex], "vkGetSemaphoreWin32HandleKHR"));

    if (!vkGetSemaphoreWin32HandleKHR)
    {
        throw std::runtime_error("Failed to get vkGetSemaphoreWin32HandleKHR function!");
    }

    if (vkGetSemaphoreWin32HandleKHR(devices[gpuIndex], &handleInfo, &handle) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to get Win32 semaphore handle!");
    }

    return handle;
}
#endif

bool VulkanBaseRenderer::importExternalImage(size_t targetGpuIndex, ExternalImage& extImage)
{
    extImage.targetGpuIndex = targetGpuIndex;

    VkExternalMemoryImageCreateInfo externalMemoryInfo{};
    externalMemoryInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    externalMemoryInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext = &externalMemoryInfo;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = extImage.width;
    imageInfo.extent.height = extImage.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = extImage.format;
    imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(devices[targetGpuIndex], &imageInfo, nullptr, &extImage.importedImage) != VK_SUCCESS)
    {
        std::println(stderr, "External memory: Failed to create imported image on GPU {}", targetGpuIndex);
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(devices[targetGpuIndex], extImage.importedImage, &memRequirements);

    // Query compatible memory types for the host pointer on the target GPU
    auto vkGetMemoryHostPointerPropertiesEXT = reinterpret_cast<PFN_vkGetMemoryHostPointerPropertiesEXT>(
        vkGetDeviceProcAddr(devices[targetGpuIndex], "vkGetMemoryHostPointerPropertiesEXT"));

    if (!vkGetMemoryHostPointerPropertiesEXT)
    {
        vkDestroyImage(devices[targetGpuIndex], extImage.importedImage, nullptr);
        extImage.importedImage = VK_NULL_HANDLE;
        std::println(stderr, "External memory: Failed to get vkGetMemoryHostPointerPropertiesEXT on GPU {}", targetGpuIndex);
        return false;
    }

    VkMemoryHostPointerPropertiesEXT hostPointerProps{};
    hostPointerProps.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT;
    if (vkGetMemoryHostPointerPropertiesEXT(devices[targetGpuIndex],
                                            VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
                                            extImage.hostPointer, &hostPointerProps) != VK_SUCCESS)
    {
        vkDestroyImage(devices[targetGpuIndex], extImage.importedImage, nullptr);
        extImage.importedImage = VK_NULL_HANDLE;
        std::println(stderr, "External memory: Failed to get host pointer properties on GPU {}", targetGpuIndex);
        return false;
    }

    VkImportMemoryHostPointerInfoEXT importHostPtrInfo{};
    importHostPtrInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT;
    importHostPtrInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    importHostPtrInfo.pHostPointer = extImage.hostPointer;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &importHostPtrInfo;
    allocInfo.allocationSize = extImage.allocationSize;
    allocInfo.memoryTypeIndex = findMemoryType(targetGpuIndex,
                                               memRequirements.memoryTypeBits & hostPointerProps.memoryTypeBits,
                                               0);

    if (vkAllocateMemory(devices[targetGpuIndex], &allocInfo, nullptr, &extImage.importedMemory) != VK_SUCCESS)
    {
        vkDestroyImage(devices[targetGpuIndex], extImage.importedImage, nullptr);
        extImage.importedImage = VK_NULL_HANDLE;
        std::println(
            stderr,
            "External memory: Failed to import host memory from GPU {} to GPU {}",
            extImage.sourceGpuIndex, targetGpuIndex);
        return false;
    }

    vkBindImageMemory(devices[targetGpuIndex], extImage.importedImage, extImage.importedMemory, 0);

    extImage.importedImageView = createImageView(devices[targetGpuIndex], extImage.importedImage,
                                                 extImage.format, VK_IMAGE_ASPECT_COLOR_BIT);

    std::println("Imported external image from GPU {} to GPU {} via host allocation", extImage.sourceGpuIndex, targetGpuIndex);
    return true;
}

void VulkanBaseRenderer::createExportableSemaphore(size_t sourceGpuIndex, VkSemaphore& semaphore)
{
#ifdef _WIN32
    VkExportSemaphoreCreateInfo exportInfo{};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = &exportInfo;

    if (vkCreateSemaphore(devices[sourceGpuIndex], &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create exportable semaphore!");
    }
#else
    throw std::runtime_error("External semaphore not supported on this platform!");
#endif
}

void VulkanBaseRenderer::importExternalSemaphore(size_t targetGpuIndex, ExternalSemaphore& extSem)
{
#ifdef _WIN32
    extSem.sharedHandle = getSemaphoreWin32Handle(extSem.sourceGpuIndex, extSem.sourceSemaphore);
    extSem.targetGpuIndex = targetGpuIndex;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    if (vkCreateSemaphore(devices[targetGpuIndex], &semaphoreInfo, nullptr, &extSem.importedSemaphore) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create semaphore for import!");
    }

    VkImportSemaphoreWin32HandleInfoKHR importInfo{};
    importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR;
    importInfo.semaphore = extSem.importedSemaphore;
    importInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    importInfo.handle = extSem.sharedHandle;

    auto vkImportSemaphoreWin32HandleKHR = reinterpret_cast<PFN_vkImportSemaphoreWin32HandleKHR>(
        vkGetDeviceProcAddr(devices[targetGpuIndex], "vkImportSemaphoreWin32HandleKHR"));

    if (!vkImportSemaphoreWin32HandleKHR)
    {
        throw std::runtime_error("Failed to get vkImportSemaphoreWin32HandleKHR function!");
    }

    if (vkImportSemaphoreWin32HandleKHR(devices[targetGpuIndex], &importInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to import semaphore!");
    }

    std::println("Imported external semaphore from GPU {} to GPU {}", extSem.sourceGpuIndex, targetGpuIndex);
#else
    throw std::runtime_error("External semaphore not supported on this platform!");
#endif
}

void VulkanBaseRenderer::destroyExternalImage(ExternalImage& extImage)
{
    if (extImage.importedImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(devices[extImage.targetGpuIndex], extImage.importedImageView, nullptr);
        extImage.importedImageView = VK_NULL_HANDLE;
    }
    if (extImage.importedImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(devices[extImage.targetGpuIndex], extImage.importedImage, nullptr);
        extImage.importedImage = VK_NULL_HANDLE;
    }
    if (extImage.importedMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(devices[extImage.targetGpuIndex], extImage.importedMemory, nullptr);
        extImage.importedMemory = VK_NULL_HANDLE;
    }

    if (extImage.sourceImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(devices[extImage.sourceGpuIndex], extImage.sourceImageView, nullptr);
        extImage.sourceImageView = VK_NULL_HANDLE;
    }
    if (extImage.sourceImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(devices[extImage.sourceGpuIndex], extImage.sourceImage, nullptr);
        extImage.sourceImage = VK_NULL_HANDLE;
    }
    if (extImage.sourceMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(devices[extImage.sourceGpuIndex], extImage.sourceMemory, nullptr);
        extImage.sourceMemory = VK_NULL_HANDLE;
    }

    if (extImage.hostPointer != nullptr)
    {
#ifdef _WIN32
        _aligned_free(extImage.hostPointer);
#else
        free(extImage.hostPointer);
#endif
        extImage.hostPointer = nullptr;
        extImage.allocationSize = 0;
    }
}

void VulkanBaseRenderer::destroyExternalSemaphore(ExternalSemaphore& extSem)
{
    if (extSem.importedSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(devices[extSem.targetGpuIndex], extSem.importedSemaphore, nullptr);
        extSem.importedSemaphore = VK_NULL_HANDLE;
    }
    if (extSem.sourceSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(devices[extSem.sourceGpuIndex], extSem.sourceSemaphore, nullptr);
        extSem.sourceSemaphore = VK_NULL_HANDLE;
    }

#ifdef _WIN32
    if (extSem.sharedHandle != nullptr)
    {
        CloseHandle(extSem.sharedHandle);
        extSem.sharedHandle = nullptr;
    }
#endif
}
