#pragma once

#include "RHITypes.h"
#include "RHIShader.h"

namespace RHI {

// Forward declarations
class RHIDescriptorSetLayout;
class RHIRenderPass;

// ============================================================================
// PIPELINE LAYOUT
// ============================================================================
// Defines the resource layout for a pipeline (descriptor sets, push constants)

struct PushConstantRange {
    ShaderStage stageFlags = ShaderStage::All;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct PipelineLayoutDesc {
    std::vector<RHIDescriptorSetLayout*> setLayouts;
    std::vector<PushConstantRange> pushConstants;
};

class RHIPipelineLayout {
public:
    virtual ~RHIPipelineLayout() = default;
    virtual void* getNativeHandle() const = 0;

protected:
    RHIPipelineLayout() = default;
};

// ============================================================================
// GRAPHICS PIPELINE
// ============================================================================

struct GraphicsPipelineDesc {
    // Shader program
    RHIShaderProgram* shaderProgram = nullptr;

    // Vertex input
    VertexInputState vertexInput;

    // Input assembly
    PrimitiveTopology primitiveTopology = PrimitiveTopology::TriangleList;
    bool primitiveRestartEnable = false;

    // Rasterization
    RasterizerState rasterizer;

    // Multisampling
    uint32_t sampleCount = 1;
    bool sampleShading = false;
    float minSampleShading = 1.0f;

    // Depth/stencil
    DepthStencilState depthStencil;

    // Color blending (per attachment)
    std::vector<BlendState> colorBlendStates;
    glm::vec4 blendConstants = glm::vec4(0.0f);

    // Dynamic state (values can be changed without recreating pipeline)
    bool dynamicViewport = true;
    bool dynamicScissor = true;
    bool dynamicLineWidth = false;
    bool dynamicDepthBias = false;
    bool dynamicBlendConstants = false;

    // Pipeline layout
    RHIPipelineLayout* layout = nullptr;

    // Render pass compatibility
    RHIRenderPass* renderPass = nullptr;
    void* nativeRenderPass = nullptr;  // Native VkRenderPass for Vulkan (used if renderPass is null)
    void* nativePipelineLayout = nullptr;  // Native VkPipelineLayout for Vulkan (used if layout is null)
    uint32_t subpass = 0;

    std::string debugName;
};

class RHIGraphicsPipeline {
public:
    virtual ~RHIGraphicsPipeline() = default;

    virtual const GraphicsPipelineDesc& getDesc() const = 0;
    virtual void* getNativeHandle() const = 0;

protected:
    RHIGraphicsPipeline() = default;
};

// ============================================================================
// COMPUTE PIPELINE
// ============================================================================

struct ComputePipelineDesc {
    RHIShaderProgram* shaderProgram = nullptr;
    RHIPipelineLayout* layout = nullptr;
    std::string debugName;
};

class RHIComputePipeline {
public:
    virtual ~RHIComputePipeline() = default;

    virtual const ComputePipelineDesc& getDesc() const = 0;
    virtual void* getNativeHandle() const = 0;

protected:
    RHIComputePipeline() = default;
};

} // namespace RHI
