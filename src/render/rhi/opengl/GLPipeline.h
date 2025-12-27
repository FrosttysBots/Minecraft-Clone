#pragma once

#include "../RHIPipeline.h"
#include "../RHIDescriptorSet.h"  // For RHIDescriptorSetLayout
#include "GLShader.h"
#include <glad/gl.h>

namespace RHI {

class GLDevice;

// ============================================================================
// GL DESCRIPTOR SET LAYOUT
// ============================================================================

class GLDescriptorSetLayout : public RHIDescriptorSetLayout {
public:
    explicit GLDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) : m_desc(desc) {}

    const DescriptorSetLayoutDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return nullptr; }  // No GL equivalent

private:
    DescriptorSetLayoutDesc m_desc;
};

// ============================================================================
// GL PIPELINE LAYOUT
// ============================================================================

class GLPipelineLayout : public RHIPipelineLayout {
public:
    explicit GLPipelineLayout(const PipelineLayoutDesc& desc) : m_desc(desc) {}

    void* getNativeHandle() const override { return nullptr; }  // No GL equivalent
    const PipelineLayoutDesc& getDesc() const { return m_desc; }

private:
    PipelineLayoutDesc m_desc;
};

// ============================================================================
// GL GRAPHICS PIPELINE
// ============================================================================

class GLGraphicsPipeline : public RHIGraphicsPipeline {
public:
    GLGraphicsPipeline(GLDevice* device, const GraphicsPipelineDesc& desc);
    ~GLGraphicsPipeline() override;

    const GraphicsPipelineDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return reinterpret_cast<void*>(static_cast<uintptr_t>(m_vao)); }

    // Apply pipeline state to OpenGL
    void bind();

    // GL-specific
    GLuint getVAO() const { return m_vao; }
    GLShaderProgram* getProgram() const { return static_cast<GLShaderProgram*>(m_desc.shaderProgram); }

private:
    void createVAO();
    void setupVertexAttributes();

    GLDevice* m_device = nullptr;
    GraphicsPipelineDesc m_desc;
    GLuint m_vao = 0;
};

// ============================================================================
// GL COMPUTE PIPELINE
// ============================================================================

class GLComputePipeline : public RHIComputePipeline {
public:
    GLComputePipeline(GLDevice* device, const ComputePipelineDesc& desc);
    ~GLComputePipeline() override = default;

    const ComputePipelineDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return nullptr; }

    void bind();

    GLShaderProgram* getProgram() const { return static_cast<GLShaderProgram*>(m_desc.shaderProgram); }

private:
    ComputePipelineDesc m_desc;
};

} // namespace RHI
