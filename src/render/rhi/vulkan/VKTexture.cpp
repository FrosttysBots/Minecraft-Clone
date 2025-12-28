#include "VKTexture.h"
#include "VKDevice.h"
#include "VKBuffer.h"
#include "VKCommandBuffer.h"
#include <cstring>
#include <iostream>
#include <algorithm>
#include <unordered_map>

namespace RHI {

// ============================================================================
// VK TEXTURE
// ============================================================================

VKTexture::VKTexture(VKDevice* device, const TextureDesc& desc)
    : m_device(device), m_desc(desc), m_ownsImage(true) {
    createImage();
    createImageView();
}

VKTexture::VKTexture(VKDevice* device, VkImage image, VkImageView view, const TextureDesc& desc)
    : m_device(device), m_desc(desc), m_image(image), m_imageView(view), m_ownsImage(false) {
    // For swapchain images, we don't own the image or view
}

VKTexture::~VKTexture() {
    if (m_ownsImage) {
        if (m_imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device->getDevice(), m_imageView, nullptr);
        }
        if (m_image != VK_NULL_HANDLE) {
            vmaDestroyImage(m_device->getAllocator(), m_image, m_allocation);
        }
    }
}

void VKTexture::createImage() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VKDevice::toVkImageType(m_desc.type);
    imageInfo.format = VKDevice::toVkFormat(m_desc.format);
    imageInfo.extent.width = m_desc.width;
    imageInfo.extent.height = m_desc.height;
    imageInfo.extent.depth = m_desc.depth;
    imageInfo.mipLevels = m_desc.mipLevels;
    imageInfo.arrayLayers = m_desc.arrayLayers;
    imageInfo.samples = static_cast<VkSampleCountFlagBits>(m_desc.samples);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Determine usage flags
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if (hasFlag(m_desc.usage, TextureUsage::RenderTarget)) {
        if (isDepthFormat(m_desc.format)) {
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        } else {
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
    }

    if (hasFlag(m_desc.usage, TextureUsage::Storage)) {
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }

    if (m_desc.mipLevels > 1) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;  // For mipmap generation
    }

    imageInfo.usage = usage;

    // Cube map flags
    if (m_desc.type == TextureType::TextureCube || m_desc.type == TextureType::TextureCubeArray) {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    // VMA allocation
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkResult result = vmaCreateImage(m_device->getAllocator(), &imageInfo, &allocInfo,
                                      &m_image, &m_allocation, nullptr);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKTexture] Failed to create image" << std::endl;
        return;
    }

    // Set debug name
    if (!m_desc.debugName.empty() && vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_image);
        nameInfo.pObjectName = m_desc.debugName.c_str();
        vkSetDebugUtilsObjectNameEXT(m_device->getDevice(), &nameInfo);
    }
}

void VKTexture::createImageView() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VKDevice::toVkImageViewType(m_desc.type);
    viewInfo.format = VKDevice::toVkFormat(m_desc.format);

    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = m_desc.arrayLayers;

    if (isDepthFormat(m_desc.format)) {
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencilComponent(m_desc.format)) {
            viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    VkResult result = vkCreateImageView(m_device->getDevice(), &viewInfo, nullptr, &m_imageView);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKTexture] Failed to create image view" << std::endl;
    }
}

void* VKTexture::getMipView(uint32_t mipLevel) {
    // Create a view for a specific mip level
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VKDevice::toVkImageViewType(m_desc.type);
    viewInfo.format = VKDevice::toVkFormat(m_desc.format);
    viewInfo.subresourceRange.aspectMask = isDepthFormat(m_desc.format) ?
        VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = mipLevel;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = m_desc.arrayLayers;

    VkImageView view;
    if (vkCreateImageView(m_device->getDevice(), &viewInfo, nullptr, &view) != VK_SUCCESS) {
        return nullptr;
    }

    // Note: Caller is responsible for destroying this view
    return view;
}

void* VKTexture::getLayerView(uint32_t arrayLayer) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;  // Single layer is always 2D
    viewInfo.format = VKDevice::toVkFormat(m_desc.format);
    viewInfo.subresourceRange.aspectMask = isDepthFormat(m_desc.format) ?
        VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = arrayLayer;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView view;
    if (vkCreateImageView(m_device->getDevice(), &viewInfo, nullptr, &view) != VK_SUCCESS) {
        return nullptr;
    }

    return view;
}

void* VKTexture::getSubresourceView(uint32_t mipLevel, uint32_t arrayLayer) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VKDevice::toVkFormat(m_desc.format);
    viewInfo.subresourceRange.aspectMask = isDepthFormat(m_desc.format) ?
        VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = mipLevel;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = arrayLayer;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView view;
    if (vkCreateImageView(m_device->getDevice(), &viewInfo, nullptr, &view) != VK_SUCCESS) {
        return nullptr;
    }

    return view;
}

void VKTexture::uploadData(const void* data, size_t dataSize,
                           uint32_t mipLevel, uint32_t arrayLayer,
                           uint32_t offsetX, uint32_t offsetY, uint32_t offsetZ,
                           uint32_t width, uint32_t height, uint32_t depth) {

    // Create staging buffer
    BufferDesc stagingDesc{};
    stagingDesc.size = dataSize;
    stagingDesc.usage = BufferUsage::TransferSrc;
    stagingDesc.memory = MemoryUsage::CpuToGpu;
    stagingDesc.debugName = "TextureStagingBuffer";

    auto stagingBuffer = std::make_unique<VKBuffer>(m_device, stagingDesc);

    // Copy data to staging buffer
    void* mapped = stagingBuffer->map();
    std::memcpy(mapped, data, dataSize);
    stagingBuffer->flush(0, dataSize);
    stagingBuffer->unmap();

    // Execute copy command
    m_device->executeImmediate([&](RHICommandBuffer* cmd) {
        auto vkCmd = static_cast<VKCommandBuffer*>(cmd)->getVkCommandBuffer();

        // Transition to transfer destination layout
        transitionLayout(vkCmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Copy buffer to image
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;    // Tightly packed
        region.bufferImageHeight = 0;  // Tightly packed

        region.imageSubresource.aspectMask = isDepthFormat(m_desc.format) ?
            VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = arrayLayer;
        region.imageSubresource.layerCount = 1;

        region.imageOffset = {static_cast<int32_t>(offsetX),
                              static_cast<int32_t>(offsetY),
                              static_cast<int32_t>(offsetZ)};
        region.imageExtent = {width, height, depth};

        vkCmdCopyBufferToImage(vkCmd, stagingBuffer->getVkBuffer(), m_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transition to shader read layout
        transitionLayout(vkCmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
}

void VKTexture::generateMipmaps() {
    if (m_desc.mipLevels <= 1) return;

    m_device->executeImmediate([&](RHICommandBuffer* cmd) {
        auto vkCmd = static_cast<VKCommandBuffer*>(cmd)->getVkCommandBuffer();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = m_image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = m_desc.arrayLayers;
        barrier.subresourceRange.levelCount = 1;

        int32_t mipWidth = m_desc.width;
        int32_t mipHeight = m_desc.height;

        for (uint32_t i = 1; i < m_desc.mipLevels; i++) {
            // Transition previous mip to transfer source
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(vkCmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, nullptr, 0, nullptr, 1, &barrier);

            // Blit to current mip
            VkImageBlit blit{};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = m_desc.arrayLayers;

            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {
                mipWidth > 1 ? mipWidth / 2 : 1,
                mipHeight > 1 ? mipHeight / 2 : 1,
                1
            };
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = m_desc.arrayLayers;

            vkCmdBlitImage(vkCmd,
                m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit, VK_FILTER_LINEAR);

            // Transition previous mip to shader read
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(vkCmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                0, nullptr, 0, nullptr, 1, &barrier);

            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }

        // Transition last mip to shader read
        barrier.subresourceRange.baseMipLevel = m_desc.mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(vkCmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);
    });

    m_currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void VKTexture::transitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout) {
    transitionLayout(cmd, m_currentLayout, newLayout);
}

void VKTexture::transitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;

    if (isDepthFormat(m_desc.format)) {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencilComponent(m_desc.format)) {
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = m_desc.mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = m_desc.arrayLayers;

    VkPipelineStageFlags srcStage;
    VkPipelineStageFlags dstStage;

    // Determine access masks and pipeline stages based on layouts
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    else {
        // Generic fallback
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    m_currentLayout = newLayout;
}

// ============================================================================
// VK SAMPLER
// ============================================================================

VKSampler::VKSampler(VKDevice* device, const SamplerDesc& desc)
    : m_device(device), m_desc(desc) {

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VKDevice::toVkFilter(desc.magFilter);
    samplerInfo.minFilter = VKDevice::toVkFilter(desc.minFilter);
    samplerInfo.mipmapMode = VKDevice::toVkMipmapMode(desc.mipmapMode);
    samplerInfo.addressModeU = VKDevice::toVkAddressMode(desc.addressU);
    samplerInfo.addressModeV = VKDevice::toVkAddressMode(desc.addressV);
    samplerInfo.addressModeW = VKDevice::toVkAddressMode(desc.addressW);
    samplerInfo.mipLodBias = desc.mipLodBias;
    samplerInfo.anisotropyEnable = desc.maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = desc.maxAnisotropy;
    samplerInfo.compareEnable = desc.compareEnable ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = VKDevice::toVkCompareOp(desc.compareOp);
    samplerInfo.minLod = desc.minLod;
    samplerInfo.maxLod = desc.maxLod;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    VkResult result = vkCreateSampler(m_device->getDevice(), &samplerInfo, nullptr, &m_sampler);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKSampler] Failed to create sampler" << std::endl;
    }
}

VKSampler::~VKSampler() {
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device->getDevice(), m_sampler, nullptr);
    }
}

} // namespace RHI
