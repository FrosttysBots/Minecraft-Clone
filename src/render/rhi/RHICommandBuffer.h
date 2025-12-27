#pragma once

#include "RHITypes.h"
#include "RHIBuffer.h"
#include "RHITexture.h"
#include "RHIPipeline.h"
#include "RHIFramebuffer.h"
#include "RHIDescriptorSet.h"

namespace RHI {

// ============================================================================
// VIEWPORT AND SCISSOR
// ============================================================================

struct Viewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct Scissor {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

// ============================================================================
// COMMAND BUFFER
// ============================================================================
// Records rendering commands for later submission
// For OpenGL: Executes commands immediately (no real command buffer)
// For Vulkan: Records to VkCommandBuffer

enum class CommandBufferLevel {
    Primary,
    Secondary
};

class RHICommandBuffer {
public:
    virtual ~RHICommandBuffer() = default;

    virtual void* getNativeHandle() const = 0;

    // ========================================================================
    // RECORDING LIFECYCLE
    // ========================================================================

    virtual void begin() = 0;
    virtual void end() = 0;
    virtual void reset() = 0;

    // ========================================================================
    // RENDER PASS COMMANDS
    // ========================================================================

    // Begin a render pass
    virtual void beginRenderPass(RHIRenderPass* renderPass,
                                  RHIFramebuffer* framebuffer,
                                  const std::vector<ClearValue>& clearValues) = 0;

    // End current render pass
    virtual void endRenderPass() = 0;

    // Next subpass (for multi-subpass render passes)
    virtual void nextSubpass() = 0;

    // ========================================================================
    // PIPELINE STATE
    // ========================================================================

    virtual void bindGraphicsPipeline(RHIGraphicsPipeline* pipeline) = 0;
    virtual void bindComputePipeline(RHIComputePipeline* pipeline) = 0;

    virtual void setViewport(const Viewport& viewport) = 0;
    virtual void setViewports(const std::vector<Viewport>& viewports) = 0;
    virtual void setScissor(const Scissor& scissor) = 0;
    virtual void setScissors(const std::vector<Scissor>& scissors) = 0;
    virtual void setLineWidth(float width) = 0;
    virtual void setDepthBias(float constantFactor, float slopeFactor) = 0;
    virtual void setBlendConstants(const glm::vec4& constants) = 0;

    // ========================================================================
    // RESOURCE BINDING
    // ========================================================================

    // Bind vertex buffers
    virtual void bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, size_t offset = 0) = 0;
    virtual void bindVertexBuffers(uint32_t firstBinding,
                                    const std::vector<RHIBuffer*>& buffers,
                                    const std::vector<size_t>& offsets) = 0;

    // Bind index buffer
    virtual void bindIndexBuffer(RHIBuffer* buffer, size_t offset = 0, bool use32Bit = true) = 0;

    // Bind descriptor sets
    virtual void bindDescriptorSet(uint32_t setIndex, RHIDescriptorSet* set,
                                    const std::vector<uint32_t>& dynamicOffsets = {}) = 0;

    // Push constants
    virtual void pushConstants(ShaderStage stages, uint32_t offset,
                                uint32_t size, const void* data) = 0;

    // ========================================================================
    // DRAW COMMANDS
    // ========================================================================

    virtual void draw(uint32_t vertexCount,
                      uint32_t instanceCount = 1,
                      uint32_t firstVertex = 0,
                      uint32_t firstInstance = 0) = 0;

    virtual void drawIndexed(uint32_t indexCount,
                              uint32_t instanceCount = 1,
                              uint32_t firstIndex = 0,
                              int32_t vertexOffset = 0,
                              uint32_t firstInstance = 0) = 0;

    // Indirect drawing
    virtual void drawIndirect(RHIBuffer* buffer,
                               size_t offset,
                               uint32_t drawCount,
                               uint32_t stride) = 0;

    virtual void drawIndexedIndirect(RHIBuffer* buffer,
                                      size_t offset,
                                      uint32_t drawCount,
                                      uint32_t stride) = 0;

    // Multi-draw indirect (single buffer, multiple draws)
    virtual void multiDrawIndirect(RHIBuffer* buffer,
                                    size_t offset,
                                    uint32_t drawCount,
                                    uint32_t stride) = 0;

    virtual void multiDrawIndexedIndirect(RHIBuffer* buffer,
                                           size_t offset,
                                           uint32_t drawCount,
                                           uint32_t stride) = 0;

    // ========================================================================
    // COMPUTE COMMANDS
    // ========================================================================

    virtual void dispatch(uint32_t groupCountX,
                          uint32_t groupCountY = 1,
                          uint32_t groupCountZ = 1) = 0;

    virtual void dispatchIndirect(RHIBuffer* buffer, size_t offset = 0) = 0;

    // ========================================================================
    // COPY COMMANDS
    // ========================================================================

    virtual void copyBuffer(RHIBuffer* src, RHIBuffer* dst,
                            size_t srcOffset, size_t dstOffset, size_t size) = 0;

    virtual void copyBufferToTexture(RHIBuffer* src, RHITexture* dst,
                                      size_t bufferOffset,
                                      uint32_t mipLevel = 0,
                                      uint32_t arrayLayer = 0) = 0;

    virtual void copyTextureToBuffer(RHITexture* src, RHIBuffer* dst,
                                      uint32_t mipLevel = 0,
                                      uint32_t arrayLayer = 0,
                                      size_t bufferOffset = 0) = 0;

    virtual void copyTexture(RHITexture* src, RHITexture* dst,
                              uint32_t srcMip = 0, uint32_t srcLayer = 0,
                              uint32_t dstMip = 0, uint32_t dstLayer = 0) = 0;

    virtual void blitTexture(RHITexture* src, RHITexture* dst,
                              const Scissor& srcRegion, const Scissor& dstRegion,
                              Filter filter = Filter::Linear) = 0;

    // ========================================================================
    // SYNCHRONIZATION
    // ========================================================================

    // Memory barrier (for buffer/image layout transitions)
    virtual void memoryBarrier() = 0;

    // Buffer memory barrier
    virtual void bufferBarrier(RHIBuffer* buffer,
                                size_t offset = 0,
                                size_t size = 0) = 0;

    // Image memory barrier / layout transition
    virtual void textureBarrier(RHITexture* texture,
                                 uint32_t baseMip = 0,
                                 uint32_t mipCount = 1,
                                 uint32_t baseLayer = 0,
                                 uint32_t layerCount = 1) = 0;

    // ========================================================================
    // DEBUG
    // ========================================================================

    virtual void beginDebugLabel(const std::string& name, const glm::vec4& color = glm::vec4(1.0f)) = 0;
    virtual void endDebugLabel() = 0;
    virtual void insertDebugLabel(const std::string& name, const glm::vec4& color = glm::vec4(1.0f)) = 0;

protected:
    RHICommandBuffer() = default;
};

} // namespace RHI
