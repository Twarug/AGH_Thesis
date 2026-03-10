#include "UnbalancedLoadCase.h"
#include "../RendererData.h"
#include "../FileUtils.h"
#include <algorithm>
#include <print>

void UnbalancedLoadScene::createPipelines(VulkanBaseRenderer* renderer)
{
    size_t deviceCount = renderer->getDeviceCount();
    heavyPipelines.resize(deviceCount);
    trivialPipelines.resize(deviceCount);

    auto vertShaderCode = FileUtils::readBinaryFile("shaders/fullscreen.vert.spv");
    auto heavyFragCode = FileUtils::readBinaryFile("shaders/fractal.frag.spv");
    auto trivialFragCode = FileUtils::readBinaryFile("shaders/trivial.frag.spv");

    for (size_t i = 0; i < deviceCount; i++)
    {
        VkDevice device = renderer->getDevice(i);
        VkPipelineLayout pipelineLayout = renderer->getPipelineLayout(i);
        VkRenderPass renderPass = renderer->getRenderPass(i);

        VkShaderModule vertModule = renderer->createShaderModulePublic(i, vertShaderCode);
        VkShaderModule heavyFragModule = renderer->createShaderModulePublic(i, heavyFragCode);
        VkShaderModule trivialFragModule = renderer->createShaderModulePublic(i, trivialFragCode);

        VkPipelineShaderStageCreateInfo vertStageInfo{};
        vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStageInfo.module = vertModule;
        vertStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo heavyFragStageInfo{};
        heavyFragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        heavyFragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        heavyFragStageInfo.module = heavyFragModule;
        heavyFragStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo trivialFragStageInfo{};
        trivialFragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        trivialFragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        trivialFragStageInfo.module = trivialFragModule;
        trivialFragStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo heavyStages[] = {vertStageInfo, heavyFragStageInfo};
        VkPipelineShaderStageCreateInfo trivialStages[] = {vertStageInfo, trivialFragStageInfo};

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;

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
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
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
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkGraphicsPipelineCreateInfo heavyPipelineInfo{};
        heavyPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        heavyPipelineInfo.stageCount = 2;
        heavyPipelineInfo.pStages = heavyStages;
        heavyPipelineInfo.pVertexInputState = &vertexInputInfo;
        heavyPipelineInfo.pInputAssemblyState = &inputAssembly;
        heavyPipelineInfo.pViewportState = &viewportState;
        heavyPipelineInfo.pRasterizationState = &rasterizer;
        heavyPipelineInfo.pMultisampleState = &multisampling;
        heavyPipelineInfo.pDepthStencilState = &depthStencil;
        heavyPipelineInfo.pColorBlendState = &colorBlending;
        heavyPipelineInfo.pDynamicState = &dynamicState;
        heavyPipelineInfo.layout = pipelineLayout;
        heavyPipelineInfo.renderPass = renderPass;
        heavyPipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &heavyPipelineInfo, nullptr, &heavyPipelines[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create heavy graphics pipeline!");
        }

        VkGraphicsPipelineCreateInfo trivialPipelineInfo = heavyPipelineInfo;
        trivialPipelineInfo.pStages = trivialStages;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &trivialPipelineInfo, nullptr, &trivialPipelines[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create trivial graphics pipeline!");
        }

                vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, heavyFragModule, nullptr);
        vkDestroyShaderModule(device, trivialFragModule, nullptr);
    }

    std::println("Created unbalanced load pipelines (heavy + trivial)");
}

void UnbalancedLoadScene::createBuffers(VulkanBaseRenderer* renderer)
{
    rendererRef = renderer;
    pushConstantsData.iterations = config.iterations;
    createPipelines(renderer);
}

void UnbalancedLoadScene::destroyBuffers(VulkanBaseRenderer* renderer)
{
    for (size_t i = 0; i < heavyPipelines.size(); i++)
    {
        if (heavyPipelines[i] != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(renderer->getDevice(i), heavyPipelines[i], nullptr);
        }
        if (trivialPipelines[i] != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(renderer->getDevice(i), trivialPipelines[i], nullptr);
        }
    }
    heavyPipelines.clear();
    trivialPipelines.clear();
    rendererRef = nullptr;
}

void UnbalancedLoadScene::recordDrawCommands(VkCommandBuffer commandBuffer, size_t gpuIndex)
{
    VkPipelineLayout layout = rendererRef->getPipelineLayout(gpuIndex);

    // The heavy/trivial split is at y=0.5 in full-screen UV space.
    // Map it to this GPU's section-local coordinates.
    glm::vec2 uvRange = rendererRef->getUVYRange(gpuIndex);
    float sectionFraction = uvRange.y - uvRange.x;  // e.g. 0.5 for 2 GPUs
    uint32_t viewportHeight = static_cast<uint32_t>(RENDER_HEIGHT * sectionFraction);
    float localSplitNorm = std::clamp((0.5f - uvRange.x) / sectionFraction, 0.0f, 1.0f);
    auto localSplitY = static_cast<uint32_t>(localSplitNorm * viewportHeight);

    // Draw heavy region (if any falls within this section)
    if ((config.heavyOnTop && localSplitY > 0) || (!config.heavyOnTop && localSplitY < viewportHeight))
    {
        VkRect2D heavyScissor{};
        if (config.heavyOnTop)
        {
            heavyScissor.offset = {0, 0};
            heavyScissor.extent = {RENDER_WIDTH, localSplitY};
        }
        else
        {
            heavyScissor.offset = {0, static_cast<int32_t>(localSplitY)};
            heavyScissor.extent = {RENDER_WIDTH, viewportHeight - localSplitY};
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, heavyPipelines[gpuIndex]);
        vkCmdSetScissor(commandBuffer, 0, 1, &heavyScissor);
        vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushConstants), &pushConstantsData);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }

    // Draw trivial region (if any falls within this section)
    if ((config.heavyOnTop && localSplitY < viewportHeight) || (!config.heavyOnTop && localSplitY > 0))
    {
        VkRect2D trivialScissor{};
        if (config.heavyOnTop)
        {
            trivialScissor.offset = {0, static_cast<int32_t>(localSplitY)};
            trivialScissor.extent = {RENDER_WIDTH, viewportHeight - localSplitY};
        }
        else
        {
            trivialScissor.offset = {0, 0};
            trivialScissor.extent = {RENDER_WIDTH, localSplitY};
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, trivialPipelines[gpuIndex]);
        vkCmdSetScissor(commandBuffer, 0, 1, &trivialScissor);
        vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushConstants), &pushConstantsData);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }
}

REGISTER_BENCHMARK_CASE(UnbalancedLoadCase)
