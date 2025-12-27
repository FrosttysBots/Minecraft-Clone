#pragma once

#include "../RHITexture.h"
#include <volk.h>
#include <vk_mem_alloc.h>

namespace RHI {

class VKDevice;

// ============================================================================
// VK TEXTURE
// ============================================================================

class VKTexture : public RHITexture {
public:
    VKTexture(VKDevice* device, const TextureDesc& desc);
    ~VKTexture() override;

    // For swapchain images (not owned)
    VKTexture(VKDevice* device, VkImage image, VkImageView view, const TextureDesc& desc);

    // Non-copyable
    VKTexture(const VKTexture&) = delete;
    VKTexture& operator=(const VKTexture&) = delete;

    // RHITexture interface
    const TextureDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return m_image; }
    void* getNativeViewHandle() const override { return m_imageView; }

    void* getMipView(uint32_t mipLevel) override;
    void* getLayerView(uint32_t arrayLayer) override;
    void* getSubresourceView(uint32_t mipLevel, uint32_t arrayLayer) override;

    void uploadData(const void* data, size_t dataSize,
                    uint32_t mipLevel, uint32_t arrayLayer,
                    uint32_t offsetX, uint32_t offsetY, uint32_t offsetZ,
                    uint32_t width, uint32_t height, uint32_t depth) override;

    void generateMipmaps() override;

    // Vulkan-specific
    VkImage getVkImage() const { return m_image; }
    VkImageView getVkImageView() const { return m_imageView; }
    VkImageLayout getCurrentLayout() const { return m_currentLayout; }
    void setCurrentLayout(VkImageLayout layout) { m_currentLayout = layout; }

    // Layout transitions
    void transitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout);
    void transitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout);

private:
    void createImage();
    void createImageView();

    VKDevice* m_device = nullptr;
    TextureDesc m_desc;
    VkImage m_image = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageLayout m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool m_ownsImage = true;  // False for swapchain images
};

// ============================================================================
// VK SAMPLER
// ============================================================================

class VKSampler : public RHISampler {
public:
    VKSampler(VKDevice* device, const SamplerDesc& desc);
    ~VKSampler() override;

    const SamplerDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return m_sampler; }

    VkSampler getVkSampler() const { return m_sampler; }

private:
    VKDevice* m_device = nullptr;
    SamplerDesc m_desc;
    VkSampler m_sampler = VK_NULL_HANDLE;
};

} // namespace RHI
