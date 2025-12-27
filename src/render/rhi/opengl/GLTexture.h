#pragma once

#include "../RHITexture.h"
#include <glad/gl.h>

namespace RHI {

class GLDevice;

// ============================================================================
// GL TEXTURE
// ============================================================================

class GLTexture : public RHITexture {
public:
    GLTexture(GLDevice* device, const TextureDesc& desc);
    ~GLTexture() override;

    // Non-copyable
    GLTexture(const GLTexture&) = delete;
    GLTexture& operator=(const GLTexture&) = delete;

    // RHITexture interface
    const TextureDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return reinterpret_cast<void*>(static_cast<uintptr_t>(m_texture)); }
    void* getNativeViewHandle() const override { return getNativeHandle(); }

    void* getMipView(uint32_t mipLevel) override;
    void* getLayerView(uint32_t arrayLayer) override;
    void* getSubresourceView(uint32_t mipLevel, uint32_t arrayLayer) override;

    void uploadData(const void* data, size_t dataSize,
                    uint32_t mipLevel, uint32_t arrayLayer,
                    uint32_t offsetX, uint32_t offsetY, uint32_t offsetZ,
                    uint32_t width, uint32_t height, uint32_t depth) override;

    void generateMipmaps() override;

    // GL-specific
    GLuint getGLTexture() const { return m_texture; }
    GLenum getGLTarget() const { return m_target; }

private:
    GLDevice* m_device = nullptr;
    TextureDesc m_desc;
    GLuint m_texture = 0;
    GLenum m_target = GL_TEXTURE_2D;
};

// ============================================================================
// GL SAMPLER
// ============================================================================

class GLSampler : public RHISampler {
public:
    GLSampler(GLDevice* device, const SamplerDesc& desc);
    ~GLSampler() override;

    const SamplerDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return reinterpret_cast<void*>(static_cast<uintptr_t>(m_sampler)); }

    GLuint getGLSampler() const { return m_sampler; }

private:
    SamplerDesc m_desc;
    GLuint m_sampler = 0;
};

} // namespace RHI
