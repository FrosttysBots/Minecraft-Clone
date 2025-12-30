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
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device->getDevice(), m_pipelineLayout, nullptr);
    }
}

void VKGraphicsPipeline::createPipeline() {
    std::cout << "[VKGraphicsPipeline] createPipeline() starting" << std::endl;

    auto* shaderProgram = static_cast<VKShaderProgram*>(m_desc.shaderProgram);
    if (!shaderProgram) {
        std::cerr << "[VKGraphicsPipeline] Error: shaderProgram is null!" << std::endl;
        return;
    }
    std::cout << "[VKGraphicsPipeline] Getting shader stages..." << std::endl;
    auto shaderStages = shaderProgram->getShaderStages();
    std::cout << "[VKGraphicsPipeline] Got " << shaderStages.size() << " shader stages" << std::endl;

    // Vertex input state
    std::cout << "[VKGraphicsPipeline] Setting up vertex input state..." << std::endl;
    std::vector<VkVertexInputBindingDescription> bindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

    std::cout << "[VKGraphicsPipeline] Vertex bindings: " << m_desc.vertexInput.bindings.size() << std::endl;
    for (const auto& binding : m_desc.vertexInput.bindings) {
        VkVertexInputBindingDescription vkBinding{};
        vkBinding.binding = binding.binding;
        vkBinding.stride = binding.stride;
        vkBinding.inputRate = binding.inputRate == VertexInputRate::Instance ?
            VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        bindingDescriptions.push_back(vkBinding);
    }

    std::cout << "[VKGraphicsPipeline] Vertex attributes: " << m_desc.vertexInput.attributes.size() << std::endl;
    for (const auto& attr : m_desc.vertexInput.attributes) {
        VkVertexInputAttributeDescription vkAttr{};
        vkAttr.location = attr.location;
        vkAttr.binding = attr.binding;
        vkAttr.format = VKDevice::toVkFormat(attr.format);
        vkAttr.offset = attr.offset;
        attributeDescriptions.push_back(vkAttr);
    }
    std::cout << "[VKGraphicsPipeline] Vertex input setup complete" << std::endl;

    std::cout << "[VKGraphicsPipeline] Creating vertex input info..." << std::endl;
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly state
    std::cout << "[VKGraphicsPipeline] Creating input assembly state..." << std::endl;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VKDevice::toVkPrimitiveTopology(m_desc.primitiveTopology);
    inputAssembly.primitiveRestartEnable = m_desc.primitiveRestartEnable ? VK_TRUE : VK_FALSE;
    std::cout << "[VKGraphicsPipeline] Input assembly done" << std::endl;

    // Viewport state (dynamic)
    std::cout << "[VKGraphicsPipeline] Creating viewport state..." << std::endl;
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterization state
    std::cout << "[VKGraphicsPipeline] Creating rasterizer state..." << std::endl;
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
    std::cout << "[VKGraphicsPipeline] Rasterizer done, lineWidth=" << rasterizer.lineWidth << std::endl;

    // Multisample state
    std::cout << "[VKGraphicsPipeline] Creating multisampling state, sampleCount=" << m_desc.sampleCount << std::endl;
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = m_desc.sampleShading ? VK_TRUE : VK_FALSE;
    // Ensure sample count is at least 1
    uint32_t sampleCount = m_desc.sampleCount > 0 ? m_desc.sampleCount : 1;
    multisampling.rasterizationSamples = static_cast<VkSampleCountFlagBits>(sampleCount);
    multisampling.minSampleShading = m_desc.minSampleShading;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;
    std::cout << "[VKGraphicsPipeline] Multisampling done" << std::endl;

    // Depth stencil state
    std::cout << "[VKGraphicsPipeline] Creating depth stencil state..." << std::endl;
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = m_desc.depthStencil.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = m_desc.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VKDevice::toVkCompareOp(m_desc.depthStencil.depthCompareOp);
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = m_desc.depthStencil.stencilTestEnable ? VK_TRUE : VK_FALSE;
    // TODO: Configure stencil ops if needed
    std::cout << "[VKGraphicsPipeline] Depth stencil done" << std::endl;

    // Color blend state
    std::cout << "[VKGraphicsPipeline] Creating color blend state, attachments=" << m_desc.colorBlendStates.size() << std::endl;
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
    for (const auto& attachment : m_desc.colorBlendStates) {
        std::cout << "[VKGraphicsPipeline] Processing blend attachment..." << std::endl;
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
    std::cout << "[VKGraphicsPipeline] Getting pipeline layout and render pass..." << std::endl;
    auto* pipelineLayout = static_cast<VKPipelineLayout*>(m_desc.layout);
    auto* renderPass = static_cast<VKRenderPass*>(m_desc.renderPass);

    // Check for native pipeline layout first
    if (m_desc.nativePipelineLayout) {
        m_pipelineLayout = static_cast<VkPipelineLayout>(m_desc.nativePipelineLayout);
        std::cout << "[VKGraphicsPipeline] Using provided native pipeline layout" << std::endl;
    } else if (!pipelineLayout) {
        std::cerr << "[VKGraphicsPipeline] Error: pipeline layout is null! Creating default layout..." << std::endl;

        // Create a descriptor set layout with a uniform buffer binding at binding 0
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        uboBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layoutCreateInfo{};
        layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutCreateInfo.bindingCount = 1;
        layoutCreateInfo.pBindings = &uboBinding;

        VkDescriptorSetLayout descriptorSetLayout;
        VkResult dsResult = vkCreateDescriptorSetLayout(m_device->getDevice(), &layoutCreateInfo, nullptr, &descriptorSetLayout);
        if (dsResult != VK_SUCCESS) {
            std::cerr << "[VKGraphicsPipeline] Failed to create descriptor set layout: " << dsResult << std::endl;
        }

        // Create pipeline layout with the descriptor set layout
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 0;

        VkResult result = vkCreatePipelineLayout(m_device->getDevice(), &layoutInfo, nullptr, &m_pipelineLayout);
        if (result != VK_SUCCESS) {
            std::cerr << "[VKGraphicsPipeline] Failed to create pipeline layout: " << result << std::endl;
        } else {
            std::cout << "[VKGraphicsPipeline] Created default pipeline layout with UBO binding" << std::endl;
        }
    }

    // Get VkRenderPass from either the wrapper or native handle
    VkRenderPass vkRenderPass = VK_NULL_HANDLE;
    if (m_desc.renderPass) {
        // Use getNativeHandle() which works for both VKRenderPass and VKSwapchainRenderPass
        vkRenderPass = static_cast<VkRenderPass>(m_desc.renderPass->getNativeHandle());
    } else if (m_desc.nativeRenderPass) {
        vkRenderPass = static_cast<VkRenderPass>(m_desc.nativeRenderPass);
    }

    if (vkRenderPass == VK_NULL_HANDLE) {
        std::cerr << "[VKGraphicsPipeline] Error: render pass is null!" << std::endl;
        return;
    }

    std::cout << "[VKGraphicsPipeline] Creating pipeline info struct..." << std::endl;

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
    pipelineInfo.layout = pipelineLayout ? pipelineLayout->getVkLayout() : m_pipelineLayout;
    pipelineInfo.renderPass = vkRenderPass;
    pipelineInfo.subpass = m_desc.subpass;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    std::cout << "[VKGraphicsPipeline] Calling vkCreateGraphicsPipelines..." << std::endl;
    std::cout << "[VKGraphicsPipeline]   renderPass=" << (void*)pipelineInfo.renderPass << std::endl;
    std::cout << "[VKGraphicsPipeline]   layout=" << (void*)pipelineInfo.layout << std::endl;
    std::cout << "[VKGraphicsPipeline]   stageCount=" << pipelineInfo.stageCount << std::endl;
    for (uint32_t i = 0; i < pipelineInfo.stageCount; i++) {
        std::cout << "[VKGraphicsPipeline]   stage[" << i << "].module=" << (void*)pipelineInfo.pStages[i].module << std::endl;
        std::cout << "[VKGraphicsPipeline]   stage[" << i << "].pName=" << pipelineInfo.pStages[i].pName << std::endl;
    }
    std::cout.flush();

    VkResult result = vkCreateGraphicsPipelines(m_device->getDevice(), VK_NULL_HANDLE, 1,
                                                 &pipelineInfo, nullptr, &m_pipeline);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKGraphicsPipeline] Failed to create graphics pipeline: VkResult=" << result << std::endl;
        return;
    }
    std::cout << "[VKGraphicsPipeline] Pipeline created successfully: " << (void*)m_pipeline << std::endl;

    // Set debug name
    if (!m_desc.debugName.empty() && vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_pipeline);
        nameInfo.pObjectName = m_desc.debugName.c_str();
        vkSetDebugUtilsObjectNameEXT(m_device->getDevice(), &nameInfo);
    }
}

VkPipelineLayout VKGraphicsPipeline::getVkLayout() const {
    if (m_desc.layout) {
        return static_cast<VKPipelineLayout*>(m_desc.layout)->getVkLayout();
    }
    return m_pipelineLayout;  // Return default/native layout
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

    // Handle null pipeline layout - create a default one
    VkPipelineLayout vkLayout = VK_NULL_HANDLE;
    if (pipelineLayout) {
        vkLayout = pipelineLayout->getVkLayout();
    } else {
        std::cerr << "[VKComputePipeline] Pipeline layout is null, creating default" << std::endl;

        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutCreateInfo{};
        layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutCreateInfo.bindingCount = 1;
        layoutCreateInfo.pBindings = &uboBinding;

        VkDescriptorSetLayout descriptorSetLayout;
        vkCreateDescriptorSetLayout(m_device->getDevice(), &layoutCreateInfo, nullptr, &descriptorSetLayout);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;

        VkResult layoutResult = vkCreatePipelineLayout(m_device->getDevice(), &layoutInfo, nullptr, &vkLayout);
        if (layoutResult != VK_SUCCESS) {
            std::cerr << "[VKComputePipeline] Failed to create pipeline layout: " << layoutResult << std::endl;
            return;
        }
        std::cout << "[VKComputePipeline] Created default pipeline layout" << std::endl;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeStage;
    pipelineInfo.layout = vkLayout;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    VkResult result = vkCreateComputePipelines(m_device->getDevice(), VK_NULL_HANDLE, 1,
                                                &pipelineInfo, nullptr, &m_pipeline);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKComputePipeline] Failed to create compute pipeline: VkResult=" << result << std::endl;
        return;
    }
    std::cout << "[VKComputePipeline] Compute pipeline created successfully: " << (void*)m_pipeline << std::endl;

    // Set debug name
    if (!m_desc.debugName.empty() && vkSetDebugUtilsObjectNameEXT) {
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
