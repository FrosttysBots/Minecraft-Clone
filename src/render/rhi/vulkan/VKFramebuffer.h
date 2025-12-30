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

// Forward declaration for VKSwapchainFramebuffer
class VKSwapchainRenderPass;
class VKSwapchainFramebuffer;

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

    // RHI swapchain interface for backend-agnostic rendering
    RHIRenderPass* getSwapchainRenderPass() override;
    RHIFramebuffer* getCurrentFramebufferRHI() override;

    // UI overlay render pass (uses loadOp=LOAD to preserve existing content)
    RHIRenderPass* getUIRenderPass();

    // Vulkan-specific
    VkSwapchainKHR getVkSwapchain() const { return m_swapchain; }
    VkFormat getVkFormat() const { return m_imageFormat; }
    VkExtent2D getExtent() const { return m_extent; }
    size_t getImageCount() const { return m_images.size(); }

    // Synchronization primitives
    VkSemaphore getImageAvailableSemaphore() const { return m_imageAvailableSemaphores[m_currentFrame]; }
    VkSemaphore getRenderFinishedSemaphore() const { return m_renderFinishedSemaphores[m_currentFrame]; }
    VkFence getInFlightFence() const { return m_inFlightFences[m_currentFrame]; }

    // Render pass and framebuffer for rendering to swapchain (Vulkan-specific accessors)
    VkRenderPass getRenderPass() const { return m_renderPass; }
    VkFramebuffer getCurrentFramebuffer() const { return m_framebuffers[m_currentImageIndex]; }

    static SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);

private:
    void createSwapchain();
    void createImageViews();
    void createDepthResources();
    void createSyncObjects();
    void createRenderPass();
    void createUIRenderPass();  // UI overlay render pass with loadOp=LOAD
    void createFramebuffers();
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

    // Render pass and framebuffers for swapchain rendering
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkRenderPass m_uiRenderPass = VK_NULL_HANDLE;  // UI overlay render pass (loadOp=LOAD)
    std::vector<VkFramebuffer> m_framebuffers;

    // Depth buffer
    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;
    VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;

    // Synchronization
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;

    uint32_t m_currentImageIndex = 0;
    uint32_t m_currentFrame = 0;

    // RHI wrappers for swapchain resources
    std::unique_ptr<VKSwapchainRenderPass> m_rhiRenderPass;
    std::unique_ptr<VKSwapchainRenderPass> m_rhiUIRenderPass;  // UI overlay render pass wrapper
    std::vector<std::unique_ptr<VKSwapchainFramebuffer>> m_rhiFramebuffers;
};

// ============================================================================
// VK SWAPCHAIN RENDER PASS (RHI wrapper)
// ============================================================================
// Lightweight wrapper around swapchain VkRenderPass for RHI interface

class VKSwapchainRenderPass : public RHIRenderPass {
public:
    VKSwapchainRenderPass(VkRenderPass renderPass, const RenderPassDesc& desc)
        : m_renderPass(renderPass), m_desc(desc) {}

    const RenderPassDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return m_renderPass; }

    VkRenderPass getVkRenderPass() const { return m_renderPass; }

private:
    VkRenderPass m_renderPass;
    RenderPassDesc m_desc;
};

// ============================================================================
// VK SWAPCHAIN FRAMEBUFFER (RHI wrapper)
// ============================================================================
// Lightweight wrapper around swapchain VkFramebuffer for RHI interface

class VKSwapchainFramebuffer : public RHIFramebuffer {
public:
    VKSwapchainFramebuffer(VkFramebuffer framebuffer, uint32_t width, uint32_t height)
        : m_framebuffer(framebuffer), m_width(width), m_height(height) {}

    const FramebufferDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return m_framebuffer; }

    uint32_t getWidth() const override { return m_width; }
    uint32_t getHeight() const override { return m_height; }

    VkFramebuffer getVkFramebuffer() const { return m_framebuffer; }

    void update(VkFramebuffer framebuffer, uint32_t width, uint32_t height) {
        m_framebuffer = framebuffer;
        m_width = width;
        m_height = height;
        m_desc.width = width;
        m_desc.height = height;
    }

private:
    VkFramebuffer m_framebuffer;
    uint32_t m_width;
    uint32_t m_height;
    FramebufferDesc m_desc;  // Keep for getDesc()
};

} // namespace RHI
