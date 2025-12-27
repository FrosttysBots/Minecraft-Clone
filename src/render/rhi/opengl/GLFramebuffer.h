#pragma once

#include "../RHIFramebuffer.h"
#include "GLTexture.h"
#include <glad/gl.h>

namespace RHI {

class GLDevice;

// ============================================================================
// GL RENDER PASS
// ============================================================================
// In OpenGL, render passes are not real objects - we just track the description
// to know how to set up framebuffer operations

class GLRenderPass : public RHIRenderPass {
public:
    explicit GLRenderPass(const RenderPassDesc& desc) : m_desc(desc) {}

    const RenderPassDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return nullptr; }

private:
    RenderPassDesc m_desc;
};

// ============================================================================
// GL FRAMEBUFFER
// ============================================================================

class GLFramebuffer : public RHIFramebuffer {
public:
    GLFramebuffer(GLDevice* device, const FramebufferDesc& desc);
    ~GLFramebuffer() override;

    const FramebufferDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return reinterpret_cast<void*>(static_cast<uintptr_t>(m_fbo)); }

    uint32_t getWidth() const override { return m_desc.width; }
    uint32_t getHeight() const override { return m_desc.height; }

    // GL-specific
    GLuint getFBO() const { return m_fbo; }
    void bind();

private:
    GLDevice* m_device = nullptr;
    FramebufferDesc m_desc;
    GLuint m_fbo = 0;
};

// ============================================================================
// GL SWAPCHAIN
// ============================================================================
// OpenGL swapchain is implicit - just wraps the default framebuffer

class GLSwapchain : public RHISwapchain {
public:
    GLSwapchain(GLDevice* device, const SwapchainDesc& desc);
    ~GLSwapchain() override = default;

    const SwapchainDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return m_window; }

    uint32_t getWidth() const override { return m_desc.width; }
    uint32_t getHeight() const override { return m_desc.height; }

    RHITexture* getCurrentTexture() override { return nullptr; }  // Default framebuffer
    uint32_t getCurrentImageIndex() const override { return 0; }

    bool acquireNextImage() override;
    bool present() override;
    void resize(uint32_t width, uint32_t height) override;

private:
    GLDevice* m_device = nullptr;
    SwapchainDesc m_desc;
    void* m_window = nullptr;
};

} // namespace RHI
