#pragma once

#include "../RHIFramebuffer.h"
#include "VKTexture.h"
#include <volk.h>
#include <vector>

namespace RHI {

class VKDevice;

// ============================================================================
// VK RENDER PASS
// ============================================================================

class VKRenderPass : public RHIRenderPass {
public:
    VKRenderPass(VKDevice* device, const RenderPassDesc& desc);
    ~VKRenderPass() override;

    const RenderPassDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return m_renderPass; }

    VkRenderPass getVkRenderPass() const { return m_renderPass; }

private:
    VKDevice* m_device = nullptr;
    RenderPassDesc m_desc;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
};

// ============================================================================
// VK FRAMEBUFFER
// ============================================================================

class VKFramebuffer : public RHIFramebuffer {
public:
    VKFramebuffer(VKDevice* device, const FramebufferDesc& desc);
    ~VKFramebuffer() override;

    const FramebufferDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return m_framebuffer; }

    uint32_t getWidth() const override { return m_desc.width; }
    uint32_t getHeight() const override { return m_desc.height; }

    VkFramebuffer getVkFramebuffer() const { return m_framebuffer; }

private:
    VKDevice* m_device = nullptr;
    FramebufferDesc m_desc;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
};

// ============================================================================
// VK SWAPCHAIN
// ============================================================================

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VKSwapchain : public RHISwapchain {
public:
    VKSwapchain(VKDevice* device, const SwapchainDesc& desc);
    ~VKSwapchain() override;

    const SwapchainDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return m_swapchain; }

    uint32_t getWidth() const override { return m_extent.width; }
    uint32_t getHeight() const override { return m_extent.height; }

    RHITexture* getCurrentTexture() override;
    uint32_t getCurrentImageIndex() const override { return m_currentImageIndex; }

    bool acquireNextImage() override;
    bool present() override;
    void resize(uint32_t width, uint32_t height) override;

    // Vulkan-specific
    VkSwapchainKHR getVkSwapchain() const { return m_swapchain; }
    VkFormat getVkFormat() const { return m_imageFormat; }
    VkExtent2D getExtent() const { return m_extent; }
    size_t getImageCount() const { return m_images.size(); }

    // Synchronization primitives
    VkSemaphore getImageAvailableSemaphore() const { return m_imageAvailableSemaphores[m_currentFrame]; }
    VkSemaphore getRenderFinishedSemaphore() const { return m_renderFinishedSemaphores[m_currentFrame]; }
    VkFence getInFlightFence() const { return m_inFlightFences[m_currentFrame]; }

    static SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);

private:
    void createSwapchain();
    void createImageViews();
    void createSyncObjects();
    void cleanup();
    void recreate();

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes);
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    VKDevice* m_device = nullptr;
    SwapchainDesc m_desc;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;

    VkFormat m_imageFormat;
    VkExtent2D m_extent;

    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    std::vector<std::unique_ptr<VKTexture>> m_textures;

    // Synchronization
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;

    uint32_t m_currentImageIndex = 0;
    uint32_t m_currentFrame = 0;
};

} // namespace RHI
