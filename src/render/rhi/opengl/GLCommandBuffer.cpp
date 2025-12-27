#include "GLCommandBuffer.h"
#include "GLDevice.h"
#include "GLBuffer.h"
#include "GLTexture.h"
#include <iostream>

namespace RHI {

GLCommandBuffer::GLCommandBuffer(GLDevice* device, CommandBufferLevel level)
    : m_device(device), m_level(level) {
}

void GLCommandBuffer::begin() {
    m_recording = true;
}

void GLCommandBuffer::end() {
    m_recording = false;
}

void GLCommandBuffer::reset() {
    m_currentGraphicsPipeline = nullptr;
    m_currentComputePipeline = nullptr;
    m_currentFramebuffer = nullptr;
    m_currentRenderPass = nullptr;
    m_indexBuffer = nullptr;
    m_recording = false;
}

// ============================================================================
// RENDER PASS COMMANDS
// ============================================================================

void GLCommandBuffer::beginRenderPass(RHIRenderPass* renderPass,
                                        RHIFramebuffer* framebuffer,
                                        const std::vector<ClearValue>& clearValues) {
    m_currentRenderPass = static_cast<GLRenderPass*>(renderPass);
    m_currentFramebuffer = static_cast<GLFramebuffer*>(framebuffer);

    if (m_currentFramebuffer) {
        m_currentFramebuffer->bind();
    } else {
        // Bind default framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // Apply clear operations based on render pass description
    if (m_currentRenderPass) {
        const auto& desc = m_currentRenderPass->getDesc();
        GLbitfield clearMask = 0;

        // Clear color attachments
        for (size_t i = 0; i < desc.colorAttachments.size() && i < clearValues.size(); i++) {
            if (desc.colorAttachments[i].loadOp == LoadOp::Clear) {
                const auto& cv = clearValues[i];
                glClearBufferfv(GL_COLOR, static_cast<GLint>(i), &cv.color.r);
            }
        }

        // Clear depth/stencil
        if (desc.hasDepthStencil && desc.depthStencilAttachment.loadOp == LoadOp::Clear) {
            size_t depthClearIndex = desc.colorAttachments.size();
            if (depthClearIndex < clearValues.size()) {
                const auto& cv = clearValues[depthClearIndex];
                glClearBufferfi(GL_DEPTH_STENCIL, 0, cv.depthStencil.depth, cv.depthStencil.stencil);
            } else {
                glClearBufferfi(GL_DEPTH_STENCIL, 0, 1.0f, 0);
            }
        }
    }
}

void GLCommandBuffer::endRenderPass() {
    m_currentRenderPass = nullptr;
    m_currentFramebuffer = nullptr;
}

void GLCommandBuffer::nextSubpass() {
    // OpenGL doesn't have subpasses - this is a no-op
}

// ============================================================================
// PIPELINE STATE
// ============================================================================

void GLCommandBuffer::bindGraphicsPipeline(RHIGraphicsPipeline* pipeline) {
    m_currentGraphicsPipeline = static_cast<GLGraphicsPipeline*>(pipeline);
    if (m_currentGraphicsPipeline) {
        m_currentGraphicsPipeline->bind();
    }
}

void GLCommandBuffer::bindComputePipeline(RHIComputePipeline* pipeline) {
    m_currentComputePipeline = static_cast<GLComputePipeline*>(pipeline);
    if (m_currentComputePipeline) {
        m_currentComputePipeline->bind();
    }
}

void GLCommandBuffer::setViewport(const Viewport& viewport) {
    glViewport(static_cast<GLint>(viewport.x), static_cast<GLint>(viewport.y),
               static_cast<GLsizei>(viewport.width), static_cast<GLsizei>(viewport.height));
    glDepthRangef(viewport.minDepth, viewport.maxDepth);
}

void GLCommandBuffer::setViewports(const std::vector<Viewport>& viewports) {
    for (size_t i = 0; i < viewports.size(); i++) {
        const auto& vp = viewports[i];
        glViewportIndexedf(static_cast<GLuint>(i), vp.x, vp.y, vp.width, vp.height);
        glDepthRangeIndexed(static_cast<GLuint>(i), vp.minDepth, vp.maxDepth);
    }
}

void GLCommandBuffer::setScissor(const Scissor& scissor) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(scissor.x, scissor.y, scissor.width, scissor.height);
}

void GLCommandBuffer::setScissors(const std::vector<Scissor>& scissors) {
    for (size_t i = 0; i < scissors.size(); i++) {
        const auto& s = scissors[i];
        glScissorIndexed(static_cast<GLuint>(i), s.x, s.y, s.width, s.height);
    }
}

void GLCommandBuffer::setLineWidth(float width) {
    glLineWidth(width);
}

void GLCommandBuffer::setDepthBias(float constantFactor, float slopeFactor) {
    glPolygonOffset(slopeFactor, constantFactor);
}

void GLCommandBuffer::setBlendConstants(const glm::vec4& constants) {
    glBlendColor(constants.r, constants.g, constants.b, constants.a);
}

// ============================================================================
// RESOURCE BINDING
// ============================================================================

void GLCommandBuffer::bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, size_t offset) {
    auto* glBuffer = static_cast<GLBuffer*>(buffer);
    if (glBuffer && m_currentGraphicsPipeline) {
        // Find stride from pipeline
        GLsizei stride = 0;
        for (const auto& b : m_currentGraphicsPipeline->getDesc().vertexInput.bindings) {
            if (b.binding == binding) {
                stride = b.stride;
                break;
            }
        }
        glBindVertexBuffer(binding, glBuffer->getGLBuffer(), static_cast<GLintptr>(offset), stride);
    }
}

void GLCommandBuffer::bindVertexBuffers(uint32_t firstBinding,
                                         const std::vector<RHIBuffer*>& buffers,
                                         const std::vector<size_t>& offsets) {
    for (size_t i = 0; i < buffers.size(); i++) {
        size_t offset = i < offsets.size() ? offsets[i] : 0;
        bindVertexBuffer(firstBinding + static_cast<uint32_t>(i), buffers[i], offset);
    }
}

void GLCommandBuffer::bindIndexBuffer(RHIBuffer* buffer, size_t offset, bool use32Bit) {
    m_indexBuffer = static_cast<GLBuffer*>(buffer);
    m_indexBufferOffset = offset;
    m_indexBuffer32Bit = use32Bit;

    if (m_indexBuffer) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer->getGLBuffer());
    }
}

void GLCommandBuffer::bindDescriptorSet(uint32_t setIndex, RHIDescriptorSet* set,
                                          const std::vector<uint32_t>& dynamicOffsets) {
    auto* glSet = static_cast<GLDescriptorSet*>(set);
    if (glSet) {
        glSet->bind(setIndex);
    }
    (void)dynamicOffsets;  // TODO: Implement dynamic offsets
}

void GLCommandBuffer::pushConstants(ShaderStage stages, uint32_t offset,
                                     uint32_t size, const void* data) {
    // OpenGL doesn't have push constants - use uniforms instead
    // This would need to be handled by the shader program
    (void)stages;
    (void)offset;
    (void)size;
    (void)data;
}

// ============================================================================
// DRAW COMMANDS
// ============================================================================

void GLCommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount,
                            uint32_t firstVertex, uint32_t firstInstance) {
    if (!m_currentGraphicsPipeline) return;

    GLenum topology = GLDevice::toGLPrimitiveTopology(m_currentGraphicsPipeline->getDesc().primitiveTopology);

    if (instanceCount > 1 || firstInstance > 0) {
        glDrawArraysInstancedBaseInstance(topology, firstVertex, vertexCount, instanceCount, firstInstance);
    } else {
        glDrawArrays(topology, firstVertex, vertexCount);
    }
}

void GLCommandBuffer::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                    uint32_t firstIndex, int32_t vertexOffset,
                                    uint32_t firstInstance) {
    if (!m_currentGraphicsPipeline || !m_indexBuffer) return;

    GLenum topology = GLDevice::toGLPrimitiveTopology(m_currentGraphicsPipeline->getDesc().primitiveTopology);
    GLenum indexType = m_indexBuffer32Bit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    size_t indexSize = m_indexBuffer32Bit ? sizeof(uint32_t) : sizeof(uint16_t);

    const void* indices = reinterpret_cast<const void*>(m_indexBufferOffset + firstIndex * indexSize);

    if (instanceCount > 1 || firstInstance > 0 || vertexOffset != 0) {
        glDrawElementsInstancedBaseVertexBaseInstance(topology, indexCount, indexType,
                                                       indices, instanceCount, vertexOffset, firstInstance);
    } else {
        glDrawElements(topology, indexCount, indexType, indices);
    }
}

void GLCommandBuffer::drawIndirect(RHIBuffer* buffer, size_t offset,
                                    uint32_t drawCount, uint32_t stride) {
    if (!m_currentGraphicsPipeline) return;

    auto* glBuffer = static_cast<GLBuffer*>(buffer);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, glBuffer->getGLBuffer());

    GLenum topology = GLDevice::toGLPrimitiveTopology(m_currentGraphicsPipeline->getDesc().primitiveTopology);

    for (uint32_t i = 0; i < drawCount; i++) {
        glDrawArraysIndirect(topology, reinterpret_cast<const void*>(offset + i * stride));
    }
}

void GLCommandBuffer::drawIndexedIndirect(RHIBuffer* buffer, size_t offset,
                                           uint32_t drawCount, uint32_t stride) {
    if (!m_currentGraphicsPipeline || !m_indexBuffer) return;

    auto* glBuffer = static_cast<GLBuffer*>(buffer);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, glBuffer->getGLBuffer());

    GLenum topology = GLDevice::toGLPrimitiveTopology(m_currentGraphicsPipeline->getDesc().primitiveTopology);
    GLenum indexType = m_indexBuffer32Bit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;

    for (uint32_t i = 0; i < drawCount; i++) {
        glDrawElementsIndirect(topology, indexType, reinterpret_cast<const void*>(offset + i * stride));
    }
}

void GLCommandBuffer::multiDrawIndirect(RHIBuffer* buffer, size_t offset,
                                         uint32_t drawCount, uint32_t stride) {
    if (!m_currentGraphicsPipeline) return;

    auto* glBuffer = static_cast<GLBuffer*>(buffer);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, glBuffer->getGLBuffer());

    GLenum topology = GLDevice::toGLPrimitiveTopology(m_currentGraphicsPipeline->getDesc().primitiveTopology);
    glMultiDrawArraysIndirect(topology, reinterpret_cast<const void*>(offset), drawCount, stride);
}

void GLCommandBuffer::multiDrawIndexedIndirect(RHIBuffer* buffer, size_t offset,
                                                uint32_t drawCount, uint32_t stride) {
    if (!m_currentGraphicsPipeline || !m_indexBuffer) return;

    auto* glBuffer = static_cast<GLBuffer*>(buffer);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, glBuffer->getGLBuffer());

    GLenum topology = GLDevice::toGLPrimitiveTopology(m_currentGraphicsPipeline->getDesc().primitiveTopology);
    GLenum indexType = m_indexBuffer32Bit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    glMultiDrawElementsIndirect(topology, indexType, reinterpret_cast<const void*>(offset), drawCount, stride);
}

// ============================================================================
// COMPUTE COMMANDS
// ============================================================================

void GLCommandBuffer::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    glDispatchCompute(groupCountX, groupCountY, groupCountZ);
}

void GLCommandBuffer::dispatchIndirect(RHIBuffer* buffer, size_t offset) {
    auto* glBuffer = static_cast<GLBuffer*>(buffer);
    glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, glBuffer->getGLBuffer());
    glDispatchComputeIndirect(static_cast<GLintptr>(offset));
}

// ============================================================================
// COPY COMMANDS
// ============================================================================

void GLCommandBuffer::copyBuffer(RHIBuffer* src, RHIBuffer* dst,
                                  size_t srcOffset, size_t dstOffset, size_t size) {
    auto* glSrc = static_cast<GLBuffer*>(src);
    auto* glDst = static_cast<GLBuffer*>(dst);

    glCopyNamedBufferSubData(glSrc->getGLBuffer(), glDst->getGLBuffer(),
                              static_cast<GLintptr>(srcOffset),
                              static_cast<GLintptr>(dstOffset),
                              static_cast<GLsizeiptr>(size));
}

void GLCommandBuffer::copyBufferToTexture(RHIBuffer* src, RHITexture* dst,
                                           size_t bufferOffset, uint32_t mipLevel,
                                           uint32_t arrayLayer) {
    auto* glSrc = static_cast<GLBuffer*>(src);
    auto* glDst = static_cast<GLTexture*>(dst);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, glSrc->getGLBuffer());
    glBindTexture(glDst->getGLTarget(), glDst->getGLTexture());

    const auto& desc = glDst->getDesc();
    GLenum format = GLDevice::toGLFormat(desc.format);
    GLenum type = GLDevice::toGLType(desc.format);

    uint32_t width = std::max(1u, desc.width >> mipLevel);
    uint32_t height = std::max(1u, desc.height >> mipLevel);

    const void* offset = reinterpret_cast<const void*>(bufferOffset);

    if (desc.type == TextureType::Texture2D) {
        glTexSubImage2D(glDst->getGLTarget(), mipLevel, 0, 0, width, height, format, type, offset);
    } else if (desc.type == TextureType::Texture2DArray || desc.type == TextureType::Texture3D) {
        glTexSubImage3D(glDst->getGLTarget(), mipLevel, 0, 0, arrayLayer, width, height, 1, format, type, offset);
    }

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(glDst->getGLTarget(), 0);
}

void GLCommandBuffer::copyTextureToBuffer(RHITexture* src, RHIBuffer* dst,
                                           uint32_t mipLevel, uint32_t arrayLayer,
                                           size_t bufferOffset) {
    auto* glSrc = static_cast<GLTexture*>(src);
    auto* glDst = static_cast<GLBuffer*>(dst);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, glDst->getGLBuffer());
    glBindTexture(glSrc->getGLTarget(), glSrc->getGLTexture());

    const auto& desc = glSrc->getDesc();
    GLenum format = GLDevice::toGLFormat(desc.format);
    GLenum type = GLDevice::toGLType(desc.format);

    void* offset = reinterpret_cast<void*>(bufferOffset);
    glGetTexImage(glSrc->getGLTarget(), mipLevel, format, type, offset);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindTexture(glSrc->getGLTarget(), 0);

    (void)arrayLayer;  // TODO: Handle array layers
}

void GLCommandBuffer::copyTexture(RHITexture* src, RHITexture* dst,
                                   uint32_t srcMip, uint32_t srcLayer,
                                   uint32_t dstMip, uint32_t dstLayer) {
    auto* glSrc = static_cast<GLTexture*>(src);
    auto* glDst = static_cast<GLTexture*>(dst);

    const auto& srcDesc = glSrc->getDesc();
    uint32_t width = std::max(1u, srcDesc.width >> srcMip);
    uint32_t height = std::max(1u, srcDesc.height >> srcMip);
    uint32_t depth = std::max(1u, srcDesc.depth >> srcMip);

    glCopyImageSubData(
        glSrc->getGLTexture(), glSrc->getGLTarget(), srcMip, 0, 0, srcLayer,
        glDst->getGLTexture(), glDst->getGLTarget(), dstMip, 0, 0, dstLayer,
        width, height, depth
    );
}

void GLCommandBuffer::blitTexture(RHITexture* src, RHITexture* dst,
                                   const Scissor& srcRegion, const Scissor& dstRegion,
                                   Filter filter) {
    // Use framebuffer blit for this
    auto* glSrc = static_cast<GLTexture*>(src);
    auto* glDst = static_cast<GLTexture*>(dst);

    GLuint srcFBO, dstFBO;
    glGenFramebuffers(1, &srcFBO);
    glGenFramebuffers(1, &dstFBO);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            glSrc->getGLTarget(), glSrc->getGLTexture(), 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFBO);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            glDst->getGLTarget(), glDst->getGLTexture(), 0);

    glBlitFramebuffer(
        srcRegion.x, srcRegion.y, srcRegion.x + srcRegion.width, srcRegion.y + srcRegion.height,
        dstRegion.x, dstRegion.y, dstRegion.x + dstRegion.width, dstRegion.y + dstRegion.height,
        GL_COLOR_BUFFER_BIT,
        GLDevice::toGLFilter(filter)
    );

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &srcFBO);
    glDeleteFramebuffers(1, &dstFBO);
}

// ============================================================================
// SYNCHRONIZATION
// ============================================================================

void GLCommandBuffer::memoryBarrier() {
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
}

void GLCommandBuffer::bufferBarrier(RHIBuffer* buffer, size_t offset, size_t size) {
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT |
                    GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_ELEMENT_ARRAY_BARRIER_BIT |
                    GL_COMMAND_BARRIER_BIT);
    (void)buffer;
    (void)offset;
    (void)size;
}

void GLCommandBuffer::textureBarrier(RHITexture* texture, uint32_t baseMip, uint32_t mipCount,
                                      uint32_t baseLayer, uint32_t layerCount) {
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_UPDATE_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
    (void)texture;
    (void)baseMip;
    (void)mipCount;
    (void)baseLayer;
    (void)layerCount;
}

// ============================================================================
// DEBUG
// ============================================================================

void GLCommandBuffer::beginDebugLabel(const std::string& name, const glm::vec4& color) {
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name.c_str());
    (void)color;
}

void GLCommandBuffer::endDebugLabel() {
    glPopDebugGroup();
}

void GLCommandBuffer::insertDebugLabel(const std::string& name, const glm::vec4& color) {
    glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 0,
                          GL_DEBUG_SEVERITY_NOTIFICATION, -1, name.c_str());
    (void)color;
}

} // namespace RHI
