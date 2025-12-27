#include "VKCommandBuffer.h"
#include "VKDevice.h"
#include "VKBuffer.h"
#include "VKTexture.h"
#include <iostream>
#include <algorithm>

namespace RHI {

VKCommandBuffer::VKCommandBuffer(VKDevice* device, CommandBufferLevel level)
    : m_device(device), m_level(level), m_commandPool(device->getCommandPool()) {

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = level == CommandBufferLevel::Primary ?
        VK_COMMAND_BUFFER_LEVEL_PRIMARY : VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    allocInfo.commandBufferCount = 1;

    VkResult result = vkAllocateCommandBuffers(m_device->getDevice(), &allocInfo, &m_commandBuffer);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKCommandBuffer] Failed to allocate command buffer" << std::endl;
    }
}

VKCommandBuffer::~VKCommandBuffer() {
    if (m_commandBuffer != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(m_device->getDevice(), m_commandPool, 1, &m_commandBuffer);
    }
}

void VKCommandBuffer::begin() {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    m_recording = true;
}

void VKCommandBuffer::end() {
    vkEndCommandBuffer(m_commandBuffer);
    m_recording = false;
}

void VKCommandBuffer::reset() {
    vkResetCommandBuffer(m_commandBuffer, 0);
    m_currentGraphicsPipeline = nullptr;
    m_currentComputePipeline = nullptr;
}

void VKCommandBuffer::beginRenderPass(RHIRenderPass* renderPass,
                                       RHIFramebuffer* framebuffer,
                                       const std::vector<ClearValue>& clearValues) {
    auto* vkRenderPass = static_cast<VKRenderPass*>(renderPass);
    auto* vkFramebuffer = static_cast<VKFramebuffer*>(framebuffer);

    std::vector<VkClearValue> vkClearValues;
    for (const auto& clear : clearValues) {
        VkClearValue vkClear{};
        // ClearValue uses a union, we use the format to determine which to use
        vkClear.color = {{clear.color.r, clear.color.g, clear.color.b, clear.color.a}};
        vkClearValues.push_back(vkClear);
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = vkRenderPass->getVkRenderPass();
    renderPassInfo.framebuffer = vkFramebuffer->getVkFramebuffer();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {vkFramebuffer->getWidth(), vkFramebuffer->getHeight()};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(vkClearValues.size());
    renderPassInfo.pClearValues = vkClearValues.data();

    vkCmdBeginRenderPass(m_commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VKCommandBuffer::endRenderPass() {
    vkCmdEndRenderPass(m_commandBuffer);
}

void VKCommandBuffer::nextSubpass() {
    vkCmdNextSubpass(m_commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
}

void VKCommandBuffer::bindGraphicsPipeline(RHIGraphicsPipeline* pipeline) {
    m_currentGraphicsPipeline = static_cast<VKGraphicsPipeline*>(pipeline);
    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_currentGraphicsPipeline->getVkPipeline());
}

void VKCommandBuffer::bindComputePipeline(RHIComputePipeline* pipeline) {
    m_currentComputePipeline = static_cast<VKComputePipeline*>(pipeline);
    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      m_currentComputePipeline->getVkPipeline());
}

void VKCommandBuffer::setViewport(const Viewport& viewport) {
    VkViewport vkViewport{};
    vkViewport.x = viewport.x;
    vkViewport.y = viewport.y;
    vkViewport.width = viewport.width;
    vkViewport.height = viewport.height;
    vkViewport.minDepth = viewport.minDepth;
    vkViewport.maxDepth = viewport.maxDepth;

    vkCmdSetViewport(m_commandBuffer, 0, 1, &vkViewport);
}

void VKCommandBuffer::setViewports(const std::vector<Viewport>& viewports) {
    std::vector<VkViewport> vkViewports;
    vkViewports.reserve(viewports.size());

    for (const auto& vp : viewports) {
        VkViewport vkViewport{};
        vkViewport.x = vp.x;
        vkViewport.y = vp.y;
        vkViewport.width = vp.width;
        vkViewport.height = vp.height;
        vkViewport.minDepth = vp.minDepth;
        vkViewport.maxDepth = vp.maxDepth;
        vkViewports.push_back(vkViewport);
    }

    vkCmdSetViewport(m_commandBuffer, 0, static_cast<uint32_t>(vkViewports.size()), vkViewports.data());
}

void VKCommandBuffer::setScissor(const Scissor& scissor) {
    VkRect2D vkScissor{};
    vkScissor.offset = {scissor.x, scissor.y};
    vkScissor.extent = {scissor.width, scissor.height};

    vkCmdSetScissor(m_commandBuffer, 0, 1, &vkScissor);
}

void VKCommandBuffer::setScissors(const std::vector<Scissor>& scissors) {
    std::vector<VkRect2D> vkScissors;
    vkScissors.reserve(scissors.size());

    for (const auto& s : scissors) {
        VkRect2D vkScissor{};
        vkScissor.offset = {s.x, s.y};
        vkScissor.extent = {s.width, s.height};
        vkScissors.push_back(vkScissor);
    }

    vkCmdSetScissor(m_commandBuffer, 0, static_cast<uint32_t>(vkScissors.size()), vkScissors.data());
}

void VKCommandBuffer::setLineWidth(float width) {
    vkCmdSetLineWidth(m_commandBuffer, width);
}

void VKCommandBuffer::setDepthBias(float constantFactor, float slopeFactor) {
    vkCmdSetDepthBias(m_commandBuffer, constantFactor, 0.0f, slopeFactor);
}

void VKCommandBuffer::setBlendConstants(const glm::vec4& constants) {
    float blendConstants[4] = {constants.r, constants.g, constants.b, constants.a};
    vkCmdSetBlendConstants(m_commandBuffer, blendConstants);
}

void VKCommandBuffer::bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, size_t offset) {
    auto* vkBuffer = static_cast<VKBuffer*>(buffer);
    VkBuffer buffers[] = {vkBuffer->getVkBuffer()};
    VkDeviceSize offsets[] = {static_cast<VkDeviceSize>(offset)};

    vkCmdBindVertexBuffers(m_commandBuffer, binding, 1, buffers, offsets);
}

void VKCommandBuffer::bindVertexBuffers(uint32_t firstBinding,
                                         const std::vector<RHIBuffer*>& buffers,
                                         const std::vector<size_t>& offsets) {
    std::vector<VkBuffer> vkBuffers;
    std::vector<VkDeviceSize> vkOffsets;
    vkBuffers.reserve(buffers.size());
    vkOffsets.reserve(offsets.size());

    for (auto* buffer : buffers) {
        vkBuffers.push_back(static_cast<VKBuffer*>(buffer)->getVkBuffer());
    }
    for (size_t offset : offsets) {
        vkOffsets.push_back(static_cast<VkDeviceSize>(offset));
    }

    vkCmdBindVertexBuffers(m_commandBuffer, firstBinding, static_cast<uint32_t>(vkBuffers.size()),
                           vkBuffers.data(), vkOffsets.data());
}

void VKCommandBuffer::bindIndexBuffer(RHIBuffer* buffer, size_t offset, bool use32Bit) {
    auto* vkBuffer = static_cast<VKBuffer*>(buffer);
    VkIndexType indexType = use32Bit ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;

    vkCmdBindIndexBuffer(m_commandBuffer, vkBuffer->getVkBuffer(),
                         static_cast<VkDeviceSize>(offset), indexType);
}

void VKCommandBuffer::bindDescriptorSet(uint32_t setIndex, RHIDescriptorSet* set,
                                         const std::vector<uint32_t>& dynamicOffsets) {
    auto* vkSet = static_cast<VKDescriptorSet*>(set);
    VkDescriptorSet sets[] = {vkSet->getVkDescriptorSet()};

    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    if (m_currentGraphicsPipeline) {
        layout = m_currentGraphicsPipeline->getVkLayout();
        bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    } else if (m_currentComputePipeline) {
        layout = m_currentComputePipeline->getVkLayout();
        bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    }

    vkCmdBindDescriptorSets(m_commandBuffer, bindPoint, layout, setIndex, 1, sets,
                            static_cast<uint32_t>(dynamicOffsets.size()), dynamicOffsets.data());
}

void VKCommandBuffer::pushConstants(ShaderStage stages, uint32_t offset,
                                     uint32_t size, const void* data) {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (m_currentGraphicsPipeline) {
        layout = m_currentGraphicsPipeline->getVkLayout();
    } else if (m_currentComputePipeline) {
        layout = m_currentComputePipeline->getVkLayout();
    }

    vkCmdPushConstants(m_commandBuffer, layout, VKDevice::toVkShaderStageFlags(stages),
                       offset, size, data);
}

void VKCommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount,
                            uint32_t firstVertex, uint32_t firstInstance) {
    vkCmdDraw(m_commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VKCommandBuffer::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                   uint32_t firstIndex, int32_t vertexOffset,
                                   uint32_t firstInstance) {
    vkCmdDrawIndexed(m_commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VKCommandBuffer::drawIndirect(RHIBuffer* buffer, size_t offset,
                                    uint32_t drawCount, uint32_t stride) {
    auto* vkBuffer = static_cast<VKBuffer*>(buffer);
    vkCmdDrawIndirect(m_commandBuffer, vkBuffer->getVkBuffer(),
                      static_cast<VkDeviceSize>(offset), drawCount, stride);
}

void VKCommandBuffer::drawIndexedIndirect(RHIBuffer* buffer, size_t offset,
                                           uint32_t drawCount, uint32_t stride) {
    auto* vkBuffer = static_cast<VKBuffer*>(buffer);
    vkCmdDrawIndexedIndirect(m_commandBuffer, vkBuffer->getVkBuffer(),
                             static_cast<VkDeviceSize>(offset), drawCount, stride);
}

void VKCommandBuffer::multiDrawIndirect(RHIBuffer* buffer, size_t offset,
                                         uint32_t drawCount, uint32_t stride) {
    drawIndirect(buffer, offset, drawCount, stride);
}

void VKCommandBuffer::multiDrawIndexedIndirect(RHIBuffer* buffer, size_t offset,
                                                uint32_t drawCount, uint32_t stride) {
    drawIndexedIndirect(buffer, offset, drawCount, stride);
}

void VKCommandBuffer::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    vkCmdDispatch(m_commandBuffer, groupCountX, groupCountY, groupCountZ);
}

void VKCommandBuffer::dispatchIndirect(RHIBuffer* buffer, size_t offset) {
    auto* vkBuffer = static_cast<VKBuffer*>(buffer);
    vkCmdDispatchIndirect(m_commandBuffer, vkBuffer->getVkBuffer(), static_cast<VkDeviceSize>(offset));
}

void VKCommandBuffer::copyBuffer(RHIBuffer* src, RHIBuffer* dst,
                                  size_t srcOffset, size_t dstOffset, size_t size) {
    auto* vkSrc = static_cast<VKBuffer*>(src);
    auto* vkDst = static_cast<VKBuffer*>(dst);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;

    vkCmdCopyBuffer(m_commandBuffer, vkSrc->getVkBuffer(), vkDst->getVkBuffer(), 1, &copyRegion);
}

void VKCommandBuffer::copyBufferToTexture(RHIBuffer* src, RHITexture* dst,
                                           size_t bufferOffset, uint32_t mipLevel,
                                           uint32_t arrayLayer) {
    auto* vkSrc = static_cast<VKBuffer*>(src);
    auto* vkDst = static_cast<VKTexture*>(dst);
    const auto& desc = vkDst->getDesc();

    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = isDepthFormat(desc.format) ?
        VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayLayer;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {
        (std::max)(1u, desc.width >> mipLevel),
        (std::max)(1u, desc.height >> mipLevel),
        (std::max)(1u, desc.depth >> mipLevel)
    };

    vkCmdCopyBufferToImage(m_commandBuffer, vkSrc->getVkBuffer(), vkDst->getVkImage(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void VKCommandBuffer::copyTextureToBuffer(RHITexture* src, RHIBuffer* dst,
                                           uint32_t mipLevel, uint32_t arrayLayer,
                                           size_t bufferOffset) {
    auto* vkSrc = static_cast<VKTexture*>(src);
    auto* vkDst = static_cast<VKBuffer*>(dst);
    const auto& desc = vkSrc->getDesc();

    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = isDepthFormat(desc.format) ?
        VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayLayer;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {
        (std::max)(1u, desc.width >> mipLevel),
        (std::max)(1u, desc.height >> mipLevel),
        (std::max)(1u, desc.depth >> mipLevel)
    };

    vkCmdCopyImageToBuffer(m_commandBuffer, vkSrc->getVkImage(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           vkDst->getVkBuffer(), 1, &region);
}

void VKCommandBuffer::copyTexture(RHITexture* src, RHITexture* dst,
                                   uint32_t srcMip, uint32_t srcLayer,
                                   uint32_t dstMip, uint32_t dstLayer) {
    auto* vkSrc = static_cast<VKTexture*>(src);
    auto* vkDst = static_cast<VKTexture*>(dst);
    const auto& srcDesc = vkSrc->getDesc();

    VkImageCopy region{};
    region.srcSubresource.aspectMask = isDepthFormat(srcDesc.format) ?
        VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel = srcMip;
    region.srcSubresource.baseArrayLayer = srcLayer;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = {0, 0, 0};

    region.dstSubresource.aspectMask = region.srcSubresource.aspectMask;
    region.dstSubresource.mipLevel = dstMip;
    region.dstSubresource.baseArrayLayer = dstLayer;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = {0, 0, 0};

    region.extent = {
        (std::max)(1u, srcDesc.width >> srcMip),
        (std::max)(1u, srcDesc.height >> srcMip),
        (std::max)(1u, srcDesc.depth >> srcMip)
    };

    vkCmdCopyImage(m_commandBuffer, vkSrc->getVkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   vkDst->getVkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void VKCommandBuffer::blitTexture(RHITexture* src, RHITexture* dst,
                                   const Scissor& srcRegion, const Scissor& dstRegion,
                                   Filter filter) {
    auto* vkSrc = static_cast<VKTexture*>(src);
    auto* vkDst = static_cast<VKTexture*>(dst);

    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0] = {srcRegion.x, srcRegion.y, 0};
    blit.srcOffsets[1] = {srcRegion.x + static_cast<int32_t>(srcRegion.width),
                          srcRegion.y + static_cast<int32_t>(srcRegion.height), 1};

    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = 0;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[0] = {dstRegion.x, dstRegion.y, 0};
    blit.dstOffsets[1] = {dstRegion.x + static_cast<int32_t>(dstRegion.width),
                          dstRegion.y + static_cast<int32_t>(dstRegion.height), 1};

    vkCmdBlitImage(m_commandBuffer, vkSrc->getVkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   vkDst->getVkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VKDevice::toVkFilter(filter));
}

void VKCommandBuffer::memoryBarrier() {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(m_commandBuffer,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VKCommandBuffer::bufferBarrier(RHIBuffer* buffer, size_t offset, size_t size) {
    auto* vkBuffer = static_cast<VKBuffer*>(buffer);

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = vkBuffer->getVkBuffer();
    barrier.offset = offset;
    barrier.size = size == 0 ? VK_WHOLE_SIZE : size;

    vkCmdPipelineBarrier(m_commandBuffer,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, nullptr, 1, &barrier, 0, nullptr);
}

void VKCommandBuffer::textureBarrier(RHITexture* texture, uint32_t baseMip, uint32_t mipCount,
                                      uint32_t baseLayer, uint32_t layerCount) {
    auto* vkTexture = static_cast<VKTexture*>(texture);
    const auto& desc = vkTexture->getDesc();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.oldLayout = vkTexture->getCurrentLayout();
    barrier.newLayout = vkTexture->getCurrentLayout();
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = vkTexture->getVkImage();
    barrier.subresourceRange.aspectMask = isDepthFormat(desc.format) ?
        VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMip;
    barrier.subresourceRange.levelCount = mipCount;
    barrier.subresourceRange.baseArrayLayer = baseLayer;
    barrier.subresourceRange.layerCount = layerCount;

    vkCmdPipelineBarrier(m_commandBuffer,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VKCommandBuffer::beginDebugLabel(const std::string& name, const glm::vec4& color) {
    VkDebugUtilsLabelEXT label{};
    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label.pLabelName = name.c_str();
    label.color[0] = color.r;
    label.color[1] = color.g;
    label.color[2] = color.b;
    label.color[3] = color.a;

    vkCmdBeginDebugUtilsLabelEXT(m_commandBuffer, &label);
}

void VKCommandBuffer::endDebugLabel() {
    vkCmdEndDebugUtilsLabelEXT(m_commandBuffer);
}

void VKCommandBuffer::insertDebugLabel(const std::string& name, const glm::vec4& color) {
    VkDebugUtilsLabelEXT label{};
    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label.pLabelName = name.c_str();
    label.color[0] = color.r;
    label.color[1] = color.g;
    label.color[2] = color.b;
    label.color[3] = color.a;

    vkCmdInsertDebugUtilsLabelEXT(m_commandBuffer, &label);
}

} // namespace RHI
