#include "GLFramebuffer.h"
#include "GLDevice.h"
#include <iostream>

namespace RHI {

// ============================================================================
// GL FRAMEBUFFER
// ============================================================================

GLFramebuffer::GLFramebuffer(GLDevice* device, const FramebufferDesc& desc)
    : m_device(device), m_desc(desc) {

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    // Attach color attachments
    std::vector<GLenum> drawBuffers;
    for (size_t i = 0; i < desc.colorAttachments.size(); i++) {
        const auto& attach = desc.colorAttachments[i];
        if (attach.texture) {
            auto* glTexture = static_cast<GLTexture*>(attach.texture);
            GLenum target = glTexture->getGLTarget();
            GLenum attachPoint = GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i);

            if (target == GL_TEXTURE_2D || target == GL_TEXTURE_2D_MULTISAMPLE) {
                glFramebufferTexture2D(GL_FRAMEBUFFER, attachPoint, target,
                                        glTexture->getGLTexture(), attach.mipLevel);
            } else if (target == GL_TEXTURE_2D_ARRAY || target == GL_TEXTURE_3D ||
                       target == GL_TEXTURE_CUBE_MAP || target == GL_TEXTURE_CUBE_MAP_ARRAY) {
                glFramebufferTextureLayer(GL_FRAMEBUFFER, attachPoint,
                                          glTexture->getGLTexture(), attach.mipLevel, attach.arrayLayer);
            } else {
                glFramebufferTexture(GL_FRAMEBUFFER, attachPoint,
                                     glTexture->getGLTexture(), attach.mipLevel);
            }

            drawBuffers.push_back(attachPoint);
        }
    }

    // Attach depth/stencil
    if (desc.depthStencilAttachment.texture) {
        auto* glTexture = static_cast<GLTexture*>(desc.depthStencilAttachment.texture);
        GLenum target = glTexture->getGLTarget();
        Format format = glTexture->getDesc().format;

        GLenum attachPoint = GL_DEPTH_ATTACHMENT;
        if (format == Format::D24_UNORM_S8_UINT || format == Format::D32_FLOAT_S8_UINT) {
            attachPoint = GL_DEPTH_STENCIL_ATTACHMENT;
        }

        if (target == GL_TEXTURE_2D || target == GL_TEXTURE_2D_MULTISAMPLE) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachPoint, target,
                                    glTexture->getGLTexture(), desc.depthStencilAttachment.mipLevel);
        } else if (target == GL_TEXTURE_2D_ARRAY || target == GL_TEXTURE_3D) {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, attachPoint,
                                      glTexture->getGLTexture(),
                                      desc.depthStencilAttachment.mipLevel,
                                      desc.depthStencilAttachment.arrayLayer);
        } else {
            glFramebufferTexture(GL_FRAMEBUFFER, attachPoint,
                                 glTexture->getGLTexture(), desc.depthStencilAttachment.mipLevel);
        }
    }

    // Set draw buffers
    if (!drawBuffers.empty()) {
        glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
    } else {
        glDrawBuffer(GL_NONE);
    }

    // Check completeness
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[GLFramebuffer] Framebuffer incomplete: 0x" << std::hex << status << std::dec << std::endl;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!desc.debugName.empty()) {
        glObjectLabel(GL_FRAMEBUFFER, m_fbo, -1, desc.debugName.c_str());
    }
}

GLFramebuffer::~GLFramebuffer() {
    if (m_fbo != 0) {
        glDeleteFramebuffers(1, &m_fbo);
    }
}

void GLFramebuffer::bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_desc.width, m_desc.height);
}

// ============================================================================
// GL SWAPCHAIN
// ============================================================================

GLSwapchain::GLSwapchain(GLDevice* device, const SwapchainDesc& desc)
    : m_device(device), m_desc(desc), m_window(desc.windowHandle) {

    // In OpenGL, the swapchain is implicit (default framebuffer)
    // Just store the window handle and dimensions

    // Enable/disable vsync
    glfwSwapInterval(desc.vsync ? 1 : 0);
}

bool GLSwapchain::acquireNextImage() {
    // In OpenGL, there's no explicit image acquisition
    return true;
}

bool GLSwapchain::present() {
    auto* window = static_cast<GLFWwindow*>(m_window);
    glfwSwapBuffers(window);

    // Check if window was resized
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    if (static_cast<uint32_t>(width) != m_desc.width ||
        static_cast<uint32_t>(height) != m_desc.height) {
        // Resize needed
        return false;
    }

    return true;
}

void GLSwapchain::resize(uint32_t width, uint32_t height) {
    m_desc.width = width;
    m_desc.height = height;
    // OpenGL handles this automatically
}

} // namespace RHI
