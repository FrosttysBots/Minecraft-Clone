#include "VKPipeline.h"
#include "VKDevice.h"
#include "VKFramebuffer.h"
#include <iostream>
#include <array>

namespace RHI {

// ============================================================================
// VK DESCRIPTOR SET LAYOUT
// ============================================================================

VKDescriptorSetLayout::VKDescriptorSetLayout(VKDevice* device, const DescriptorSetLayoutDesc& desc)
    : m_device(device), m_desc(desc) {

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(desc.bindings.size());

    for (const auto& binding : desc.bindings) {
        VkDescriptorSetLayoutBinding vkBinding{};
        vkBinding.binding = binding.binding;
        vkBinding.descriptorType = VKDevice::toVkDescriptorType(binding.type);
        vkBinding.descriptorCount = binding.count;
        vkBinding.stageFlags = VKDevice::toVkShaderStageFlags(binding.stageFlags);
        vkBinding.pImmutableSamplers = nullptr;

        bindings.push_back(vkBinding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkResult result = vkCreateDescriptorSetLayout(m_device->getDevice(), &layoutInfo, nullptr, &m_layout);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKDescriptorSetLayout] Failed to create descriptor set layout" << std::endl;
    }
}

VKDescriptorSetLayout::~VKDescriptorSetLayout() {
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device->getDevice(), m_layout, nullptr);
    }
}

// ============================================================================
// VK PIPELINE LAYOUT
// ============================================================================

VKPipelineLayout::VKPipelineLayout(VKDevice* device, const PipelineLayoutDesc& desc)
    : m_device(device) {

    std::vector<VkDescriptorSetLayout> setLayouts;
    setLayouts.reserve(desc.setLayouts.size());

    for (auto* layout : desc.setLayouts) {
        auto* vkLayout = static_cast<VKDescriptorSetLayout*>(layout);
        setLayouts.push_back(vkLayout->getVkLayout());
    }

    std::vector<VkPushConstantRange> pushConstantRanges;
    pushConstantRanges.reserve(desc.pushConstants.size());

    for (const auto& range : desc.pushConstants) {
        VkPushConstantRange vkRange{};
        vkRange.stageFlags = VKDevice::toVkShaderStageFlags(range.stageFlags);
        vkRange.offset = range.offset;
        vkRange.size = range.size;
        pushConstantRanges.push_back(vkRange);
    }

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
    layoutInfo.pPushConstantRanges = pushConstantRanges.data();

    VkResult result = vkCreatePipelineLayout(m_device->getDevice(), &layoutInfo, nullptr, &m_layout);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKPipelineLayout] Failed to create pipeline layout" << std::endl;
    }
}

VKPipelineLayout::~VKPipelineLayout() {
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device->getDevice(), m_layout, nullptr);
    }
}

// ============================================================================
// VK GRAPHICS PIPELINE
// ============================================================================

VKGraphicsPipeline::VKGraphicsPipeline(VKDevice* device, const GraphicsPipelineDesc& desc)
    : m_device(device), m_desc(desc) {
    createPipeline();
}

VKGraphicsPipeline::~VKGraphicsPipeline() {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device->getDevice(), m_pipeline, nullptr);
    }
}

void VKGraphicsPipeline::createPipeline() {
    auto* shaderProgram = static_cast<VKShaderProgram*>(m_desc.shaderProgram);
    auto shaderStages = shaderProgram->getShaderStages();

    // Vertex input state
    std::vector<VkVertexInputBindingDescription> bindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

    for (const auto& binding : m_desc.vertexInput.bindings) {
        VkVertexInputBindingDescription vkBinding{};
        vkBinding.binding = binding.binding;
        vkBinding.stride = binding.stride;
        vkBinding.inputRate = binding.inputRate == VertexInputRate::Instance ?
            VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        bindingDescriptions.push_back(vkBinding);
    }

    for (const auto& attr : m_desc.vertexInput.attributes) {
        VkVertexInputAttributeDescription vkAttr{};
        vkAttr.location = attr.location;
        vkAttr.binding = attr.binding;
        vkAttr.format = VKDevice::toVkFormat(attr.format);
        vkAttr.offset = attr.offset;
        attributeDescriptions.push_back(vkAttr);
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly state
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VKDevice::toVkPrimitiveTopology(m_desc.primitiveTopology);
    inputAssembly.primitiveRestartEnable = m_desc.primitiveRestartEnable ? VK_TRUE : VK_FALSE;

    // Viewport state (dynamic)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = m_desc.rasterizer.depthClampEnable ? VK_TRUE : VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VKDevice::toVkPolygonMode(m_desc.rasterizer.polygonMode);
    rasterizer.cullMode = VKDevice::toVkCullMode(m_desc.rasterizer.cullMode);
    rasterizer.frontFace = VKDevice::toVkFrontFace(m_desc.rasterizer.frontFace);
    rasterizer.depthBiasEnable = m_desc.rasterizer.depthBiasEnable ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = m_desc.rasterizer.depthBiasConstant;
    rasterizer.depthBiasSlopeFactor = m_desc.rasterizer.depthBiasSlope;
    rasterizer.depthBiasClamp = 0.0f;  // Not in RasterizerState, default to 0
    rasterizer.lineWidth = m_desc.rasterizer.lineWidth;

    // Multisample state
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = m_desc.sampleShading ? VK_TRUE : VK_FALSE;
    multisampling.rasterizationSamples = static_cast<VkSampleCountFlagBits>(m_desc.sampleCount);
    multisampling.minSampleShading = m_desc.minSampleShading;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    // Depth stencil state
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = m_desc.depthStencil.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = m_desc.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VKDevice::toVkCompareOp(m_desc.depthStencil.depthCompareOp);
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = m_desc.depthStencil.stencilTestEnable ? VK_TRUE : VK_FALSE;
    // TODO: Configure stencil ops if needed

    // Color blend state
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
    for (const auto& attachment : m_desc.colorBlendStates) {
        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = attachment.colorWriteMask;
        blendAttachment.blendEnable = attachment.enable ? VK_TRUE : VK_FALSE;
        blendAttachment.srcColorBlendFactor = VKDevice::toVkBlendFactor(attachment.srcColorFactor);
        blendAttachment.dstColorBlendFactor = VKDevice::toVkBlendFactor(attachment.dstColorFactor);
        blendAttachment.colorBlendOp = VKDevice::toVkBlendOp(attachment.colorOp);
        blendAttachment.srcAlphaBlendFactor = VKDevice::toVkBlendFactor(attachment.srcAlphaFactor);
        blendAttachment.dstAlphaBlendFactor = VKDevice::toVkBlendFactor(attachment.dstAlphaFactor);
        blendAttachment.alphaBlendOp = VKDevice::toVkBlendOp(attachment.alphaOp);
        colorBlendAttachments.push_back(blendAttachment);
    }

    // Default attachment if none specified
    if (colorBlendAttachments.empty()) {
        VkPipelineColorBlendAttachmentState defaultAttachment{};
        defaultAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        defaultAttachment.blendEnable = VK_FALSE;
        colorBlendAttachments.push_back(defaultAttachment);
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();

    // Dynamic state
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    if (m_desc.dynamicLineWidth) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_LINE_WIDTH);
    }
    if (m_desc.dynamicDepthBias) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
    }

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Get pipeline layout
    auto* pipelineLayout = static_cast<VKPipelineLayout*>(m_desc.layout);
    auto* renderPass = static_cast<VKRenderPass*>(m_desc.renderPass);

    // Create the pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pTessellationState = nullptr;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout->getVkLayout();
    pipelineInfo.renderPass = renderPass->getVkRenderPass();
    pipelineInfo.subpass = m_desc.subpass;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    VkResult result = vkCreateGraphicsPipelines(m_device->getDevice(), VK_NULL_HANDLE, 1,
                                                 &pipelineInfo, nullptr, &m_pipeline);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKGraphicsPipeline] Failed to create graphics pipeline" << std::endl;
        return;
    }

    // Set debug name
    if (!m_desc.debugName.empty()) {
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_pipeline);
        nameInfo.pObjectName = m_desc.debugName.c_str();
        vkSetDebugUtilsObjectNameEXT(m_device->getDevice(), &nameInfo);
    }
}

VkPipelineLayout VKGraphicsPipeline::getVkLayout() const {
    return static_cast<VKPipelineLayout*>(m_desc.layout)->getVkLayout();
}

// ============================================================================
// VK COMPUTE PIPELINE
// ============================================================================

VKComputePipeline::VKComputePipeline(VKDevice* device, const ComputePipelineDesc& desc)
    : m_device(device), m_desc(desc) {

    auto* shaderProgram = static_cast<VKShaderProgram*>(m_desc.shaderProgram);
    auto shaderStages = shaderProgram->getShaderStages();

    // Find compute stage
    VkPipelineShaderStageCreateInfo computeStage{};
    bool foundCompute = false;
    for (const auto& stage : shaderStages) {
        if (stage.stage == VK_SHADER_STAGE_COMPUTE_BIT) {
            computeStage = stage;
            foundCompute = true;
            break;
        }
    }

    if (!foundCompute) {
        std::cerr << "[VKComputePipeline] No compute shader found in program" << std::endl;
        return;
    }

    auto* pipelineLayout = static_cast<VKPipelineLayout*>(m_desc.layout);

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeStage;
    pipelineInfo.layout = pipelineLayout->getVkLayout();
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    VkResult result = vkCreateComputePipelines(m_device->getDevice(), VK_NULL_HANDLE, 1,
                                                &pipelineInfo, nullptr, &m_pipeline);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKComputePipeline] Failed to create compute pipeline" << std::endl;
        return;
    }

    // Set debug name
    if (!m_desc.debugName.empty()) {
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_pipeline);
        nameInfo.pObjectName = m_desc.debugName.c_str();
        vkSetDebugUtilsObjectNameEXT(m_device->getDevice(), &nameInfo);
    }
}

VKComputePipeline::~VKComputePipeline() {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device->getDevice(), m_pipeline, nullptr);
    }
}

VkPipelineLayout VKComputePipeline::getVkLayout() const {
    return static_cast<VKPipelineLayout*>(m_desc.layout)->getVkLayout();
}

} // namespace RHI
