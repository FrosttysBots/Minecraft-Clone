#pragma once

#include "../RHICommandBuffer.h"
#include "VKPipeline.h"
#include "VKFramebuffer.h"
#include "VKDescriptorSet.h"
#include <volk.h>

namespace RHI {

class VKDevice;

// ============================================================================
// VK COMMAND BUFFER
// ============================================================================

class VKCommandBuffer : public RHICommandBuffer {
public:
    VKCommandBuffer(VKDevice* device, CommandBufferLevel level);
    ~VKCommandBuffer() override;

    void* getNativeHandle() const override { return m_commandBuffer; }

    // Recording lifecycle
    void begin() override;
    void end() override;
    void reset() override;

    // Render pass commands
    void beginRenderPass(RHIRenderPass* renderPass,
                          RHIFramebuffer* framebuffer,
                          const std::vector<ClearValue>& clearValues) override;
    void endRenderPass() override;
    void nextSubpass() override;

    // Pipeline state
    void bindGraphicsPipeline(RHIGraphicsPipeline* pipeline) override;
    void bindComputePipeline(RHIComputePipeline* pipeline) override;

    void setViewport(const Viewport& viewport) override;
    void setViewports(const std::vector<Viewport>& viewports) override;
    void setScissor(const Scissor& scissor) override;
    void setScissors(const std::vector<Scissor>& scissors) override;
    void setLineWidth(float width) override;
    void setDepthBias(float constantFactor, float slopeFactor) override;
    void setBlendConstants(const glm::vec4& constants) override;

    // Resource binding
    void bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, size_t offset) override;
    void bindVertexBuffers(uint32_t firstBinding,
                            const std::vector<RHIBuffer*>& buffers,
                            const std::vector<size_t>& offsets) override;
    void bindIndexBuffer(RHIBuffer* buffer, size_t offset, bool use32Bit) override;
    void bindDescriptorSet(uint32_t setIndex, RHIDescriptorSet* set,
                            const std::vector<uint32_t>& dynamicOffsets) override;
    void pushConstants(ShaderStage stages, uint32_t offset,
                        uint32_t size, const void* data) override;

    // Draw commands
    void draw(uint32_t vertexCount, uint32_t instanceCount,
              uint32_t firstVertex, uint32_t firstInstance) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                      uint32_t firstIndex, int32_t vertexOffset,
                      uint32_t firstInstance) override;
    void drawIndirect(RHIBuffer* buffer, size_t offset,
                       uint32_t drawCount, uint32_t stride) override;
    void drawIndexedIndirect(RHIBuffer* buffer, size_t offset,
                              uint32_t drawCount, uint32_t stride) override;
    void multiDrawIndirect(RHIBuffer* buffer, size_t offset,
                            uint32_t drawCount, uint32_t stride) override;
    void multiDrawIndexedIndirect(RHIBuffer* buffer, size_t offset,
                                   uint32_t drawCount, uint32_t stride) override;

    // Compute commands
    void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;
    void dispatchIndirect(RHIBuffer* buffer, size_t offset) override;

    // Copy commands
    void copyBuffer(RHIBuffer* src, RHIBuffer* dst,
                    size_t srcOffset, size_t dstOffset, size_t size) override;
    void copyBufferToTexture(RHIBuffer* src, RHITexture* dst,
                              size_t bufferOffset, uint32_t mipLevel,
                              uint32_t arrayLayer) override;
    void copyTextureToBuffer(RHITexture* src, RHIBuffer* dst,
                              uint32_t mipLevel, uint32_t arrayLayer,
                              size_t bufferOffset) override;
    void copyTexture(RHITexture* src, RHITexture* dst,
                      uint32_t srcMip, uint32_t srcLayer,
                      uint32_t dstMip, uint32_t dstLayer) override;
    void blitTexture(RHITexture* src, RHITexture* dst,
                      const Scissor& srcRegion, const Scissor& dstRegion,
                      Filter filter) override;

    // Synchronization
    void memoryBarrier() override;
    void bufferBarrier(RHIBuffer* buffer, size_t offset, size_t size) override;
    void textureBarrier(RHITexture* texture, uint32_t baseMip, uint32_t mipCount,
                         uint32_t baseLayer, uint32_t layerCount) override;

    // Debug
    void beginDebugLabel(const std::string& name, const glm::vec4& color) override;
    void endDebugLabel() override;
    void insertDebugLabel(const std::string& name, const glm::vec4& color) override;

    // Vulkan-specific
    VkCommandBuffer getVkCommandBuffer() const { return m_commandBuffer; }

private:
    VKDevice* m_device = nullptr;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;  // Owned by device, not freed here
    CommandBufferLevel m_level;
    bool m_recording = false;

    // Current state
    VKGraphicsPipeline* m_currentGraphicsPipeline = nullptr;
    VKComputePipeline* m_currentComputePipeline = nullptr;
};

} // namespace RHI
