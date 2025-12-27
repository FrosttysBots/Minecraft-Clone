#include "GLPipeline.h"
#include "GLDevice.h"
#include <iostream>

namespace RHI {

// ============================================================================
// GL GRAPHICS PIPELINE
// ============================================================================

GLGraphicsPipeline::GLGraphicsPipeline(GLDevice* device, const GraphicsPipelineDesc& desc)
    : m_device(device), m_desc(desc) {
    createVAO();
}

GLGraphicsPipeline::~GLGraphicsPipeline() {
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
    }
}

void GLGraphicsPipeline::createVAO() {
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    setupVertexAttributes();
    glBindVertexArray(0);

    if (!m_desc.debugName.empty()) {
        glObjectLabel(GL_VERTEX_ARRAY, m_vao, -1, m_desc.debugName.c_str());
    }
}

void GLGraphicsPipeline::setupVertexAttributes() {
    // Note: Actual buffer binding happens when drawing
    // Here we just configure the attribute format

    for (const auto& attr : m_desc.vertexInput.attributes) {
        glEnableVertexAttribArray(attr.location);

        GLenum type = GL_FLOAT;
        GLint size = 4;
        GLboolean normalized = GL_FALSE;

        // Determine type and size from format
        switch (attr.format) {
            case Format::R32_FLOAT:
                type = GL_FLOAT; size = 1;
                break;
            case Format::RG32_FLOAT:
                type = GL_FLOAT; size = 2;
                break;
            case Format::RGBA32_FLOAT:
                type = GL_FLOAT; size = 4;
                break;
            case Format::R32_SINT:
            case Format::R32_UINT:
                type = GL_INT; size = 1;
                break;
            case Format::RGBA8_UNORM:
                type = GL_UNSIGNED_BYTE; size = 4; normalized = GL_TRUE;
                break;
            case Format::RGB10A2_UNORM:
                type = GL_UNSIGNED_INT_2_10_10_10_REV; size = 4; normalized = GL_TRUE;
                break;
            default:
                type = GL_FLOAT; size = 4;
                break;
        }

        // Find stride from binding
        GLsizei stride = 0;
        for (const auto& binding : m_desc.vertexInput.bindings) {
            if (binding.binding == attr.binding) {
                stride = binding.stride;
                break;
            }
        }

        // For integer formats, use glVertexAttribIFormat
        bool isInteger = (type == GL_INT || type == GL_UNSIGNED_INT ||
                          type == GL_SHORT || type == GL_UNSIGNED_SHORT ||
                          type == GL_BYTE || type == GL_UNSIGNED_BYTE) && !normalized;

        if (isInteger) {
            glVertexAttribIFormat(attr.location, size, type, attr.offset);
        } else {
            glVertexAttribFormat(attr.location, size, type, normalized, attr.offset);
        }

        glVertexAttribBinding(attr.location, attr.binding);
    }

    // Set up binding divisors for instancing
    for (const auto& binding : m_desc.vertexInput.bindings) {
        GLuint divisor = (binding.inputRate == VertexInputRate::Instance) ? 1 : 0;
        glVertexBindingDivisor(binding.binding, divisor);
    }
}

void GLGraphicsPipeline::bind() {
    // Bind VAO
    glBindVertexArray(m_vao);

    // Bind shader program
    auto* program = getProgram();
    if (program) {
        glUseProgram(program->getGLProgram());
    }

    // Apply rasterizer state
    const auto& raster = m_desc.rasterizer;

    // Cull mode
    if (raster.cullMode == CullMode::None) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(GLDevice::toGLCullMode(raster.cullMode));
    }

    // Front face
    glFrontFace(raster.frontFace == FrontFace::CounterClockwise ? GL_CCW : GL_CW);

    // Polygon mode
    glPolygonMode(GL_FRONT_AND_BACK, GLDevice::toGLPolygonMode(raster.polygonMode));

    // Depth clamp
    if (raster.depthClampEnable) {
        glEnable(GL_DEPTH_CLAMP);
    } else {
        glDisable(GL_DEPTH_CLAMP);
    }

    // Depth bias
    if (raster.depthBiasEnable) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(raster.depthBiasSlope, raster.depthBiasConstant);
    } else {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    // Line width
    glLineWidth(raster.lineWidth);

    // Apply depth/stencil state
    const auto& ds = m_desc.depthStencil;

    if (ds.depthTestEnable) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GLDevice::toGLCompareOp(ds.depthCompareOp));
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    glDepthMask(ds.depthWriteEnable ? GL_TRUE : GL_FALSE);

    if (ds.stencilTestEnable) {
        glEnable(GL_STENCIL_TEST);
    } else {
        glDisable(GL_STENCIL_TEST);
    }

    // Apply blend state
    if (!m_desc.colorBlendStates.empty()) {
        bool anyBlendEnabled = false;
        for (size_t i = 0; i < m_desc.colorBlendStates.size(); i++) {
            const auto& blend = m_desc.colorBlendStates[i];

            if (blend.enable) {
                anyBlendEnabled = true;
                glEnablei(GL_BLEND, static_cast<GLuint>(i));
                glBlendFuncSeparatei(static_cast<GLuint>(i),
                    GLDevice::toGLBlendFactor(blend.srcColorFactor),
                    GLDevice::toGLBlendFactor(blend.dstColorFactor),
                    GLDevice::toGLBlendFactor(blend.srcAlphaFactor),
                    GLDevice::toGLBlendFactor(blend.dstAlphaFactor));
                glBlendEquationSeparatei(static_cast<GLuint>(i),
                    GLDevice::toGLBlendOp(blend.colorOp),
                    GLDevice::toGLBlendOp(blend.alphaOp));
            } else {
                glDisablei(GL_BLEND, static_cast<GLuint>(i));
            }

            glColorMaski(static_cast<GLuint>(i),
                (blend.colorWriteMask & 0x1) ? GL_TRUE : GL_FALSE,
                (blend.colorWriteMask & 0x2) ? GL_TRUE : GL_FALSE,
                (blend.colorWriteMask & 0x4) ? GL_TRUE : GL_FALSE,
                (blend.colorWriteMask & 0x8) ? GL_TRUE : GL_FALSE);
        }

        if (anyBlendEnabled) {
            glBlendColor(m_desc.blendConstants.r, m_desc.blendConstants.g,
                         m_desc.blendConstants.b, m_desc.blendConstants.a);
        }
    }
}

// ============================================================================
// GL COMPUTE PIPELINE
// ============================================================================

GLComputePipeline::GLComputePipeline(GLDevice* device, const ComputePipelineDesc& desc)
    : m_desc(desc) {
    (void)device;
}

void GLComputePipeline::bind() {
    auto* program = getProgram();
    if (program) {
        glUseProgram(program->getGLProgram());
    }
}

} // namespace RHI
