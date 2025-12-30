#pragma once

#include "RHITypes.h"
#include "RHITexture.h"

namespace RHI {

// ============================================================================
// RENDER PASS
// ============================================================================
// Defines the structure of a render pass (attachments, subpasses)
// For OpenGL: Used to track state, not a real object
// For Vulkan: Maps to VkRenderPass

struct RenderPassDesc {
    std::vector<AttachmentDesc> colorAttachments;
    AttachmentDesc depthStencilAttachment;
    bool hasDepthStencil = false;
    std::string debugName;
};

class RHIRenderPass {
public:
    virtual ~RHIRenderPass() = default;

    virtual const RenderPassDesc& getDesc() const = 0;
    virtual void* getNativeHandle() const = 0;

protected:
    RHIRenderPass() = default;
};

// ============================================================================
// FRAMEBUFFER
// ============================================================================
// A collection of textures used as render targets
// For OpenGL: FBO
// For Vulkan: VkFramebuffer

struct FramebufferAttachment {
    RHITexture* texture = nullptr;
    uint32_t mipLevel = 0;
    uint32_t arrayLayer = 0;  // For cube maps: 0-5, for arrays: layer index
};

struct FramebufferDesc {
    RHIRenderPass* renderPass = nullptr;
    std::vector<FramebufferAttachment> colorAttachments;
    FramebufferAttachment depthStencilAttachment;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t layers = 1;  // For layered rendering
    std::string debugName;
};

class RHIFramebuffer {
public:
    virtual ~RHIFramebuffer() = default;

    virtual const FramebufferDesc& getDesc() const = 0;
    virtual void* getNativeHandle() const = 0;

    virtual uint32_t getWidth() const = 0;
    virtual uint32_t getHeight() const = 0;

protected:
    RHIFramebuffer() = default;
};

// ============================================================================
// SWAPCHAIN
// ============================================================================
// Represents the window surface for presentation
// For OpenGL: Implicit (window's default framebuffer)
// For Vulkan: VkSwapchainKHR

struct SwapchainDesc {
    void* windowHandle = nullptr;  // GLFW window
    uint32_t width = 0;
    uint32_t height = 0;
    Format format = Format::BGRA8_SRGB;
    uint32_t imageCount = 3;  // Triple buffering
    bool vsync = true;
};

class RHISwapchain {
public:
    virtual ~RHISwapchain() = default;

    virtual const SwapchainDesc& getDesc() const = 0;
    virtual void* getNativeHandle() const = 0;

    // Get current dimensions
    virtual uint32_t getWidth() const = 0;
    virtual uint32_t getHeight() const = 0;

    // Get current back buffer texture
    virtual RHITexture* getCurrentTexture() = 0;

    // Get current back buffer index
    virtual uint32_t getCurrentImageIndex() const = 0;

    // Acquire next image for rendering
    // Returns false if resize is needed
    virtual bool acquireNextImage() = 0;

    // Present the current image
    // Returns false if resize is needed
    virtual bool present() = 0;

    // Resize swapchain (call after window resize)
    virtual void resize(uint32_t width, uint32_t height) = 0;

    // Get render pass for swapchain rendering (backend-specific)
    // For Vulkan: Returns the VkRenderPass for swapchain images
    // For OpenGL: Returns a render pass descriptor for the default framebuffer
    virtual RHIRenderPass* getSwapchainRenderPass() = 0;

    // Get current framebuffer for swapchain rendering (backend-specific)
    // For Vulkan: Returns VkFramebuffer for current swapchain image
    // For OpenGL: Returns wrapper for default framebuffer (FBO 0)
    virtual RHIFramebuffer* getCurrentFramebufferRHI() = 0;

protected:
    RHISwapchain() = default;
};

} // namespace RHI
