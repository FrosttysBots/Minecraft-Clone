#include "GLTexture.h"
#include "GLDevice.h"
#include <iostream>

namespace RHI {

GLTexture::GLTexture(GLDevice* device, const TextureDesc& desc)
    : m_device(device), m_desc(desc) {

    m_target = GLDevice::toGLTextureTarget(desc.type, desc.samples);

    glGenTextures(1, &m_texture);
    glBindTexture(m_target, m_texture);

    GLenum internalFormat = GLDevice::toGLInternalFormat(desc.format);

    switch (desc.type) {
        case TextureType::Texture1D:
            glTexStorage1D(m_target, desc.mipLevels, internalFormat, desc.width);
            break;

        case TextureType::Texture2D:
            if (desc.samples > 1) {
                glTexStorage2DMultisample(m_target, desc.samples, internalFormat,
                                          desc.width, desc.height, GL_TRUE);
            } else {
                glTexStorage2D(m_target, desc.mipLevels, internalFormat,
                               desc.width, desc.height);
            }
            break;

        case TextureType::Texture3D:
            glTexStorage3D(m_target, desc.mipLevels, internalFormat,
                           desc.width, desc.height, desc.depth);
            break;

        case TextureType::TextureCube:
            glTexStorage2D(m_target, desc.mipLevels, internalFormat,
                           desc.width, desc.height);
            break;

        case TextureType::Texture2DArray:
            if (desc.samples > 1) {
                glTexStorage3DMultisample(m_target, desc.samples, internalFormat,
                                          desc.width, desc.height, desc.arrayLayers, GL_TRUE);
            } else {
                glTexStorage3D(m_target, desc.mipLevels, internalFormat,
                               desc.width, desc.height, desc.arrayLayers);
            }
            break;

        case TextureType::TextureCubeArray:
            glTexStorage3D(m_target, desc.mipLevels, internalFormat,
                           desc.width, desc.height, desc.arrayLayers * 6);
            break;
    }

    // Set default sampling parameters
    if (desc.samples <= 1) {
        glTexParameteri(m_target, GL_TEXTURE_MIN_FILTER,
                        desc.mipLevels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(m_target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(m_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(m_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(m_target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }

    glBindTexture(m_target, 0);

    if (!desc.debugName.empty()) {
        glObjectLabel(GL_TEXTURE, m_texture, -1, desc.debugName.c_str());
    }
}

GLTexture::~GLTexture() {
    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
    }
}

void* GLTexture::getMipView(uint32_t mipLevel) {
    // In OpenGL, we use texture views for this
    // For simplicity, just return the texture handle
    (void)mipLevel;
    return getNativeHandle();
}

void* GLTexture::getLayerView(uint32_t arrayLayer) {
    (void)arrayLayer;
    return getNativeHandle();
}

void* GLTexture::getSubresourceView(uint32_t mipLevel, uint32_t arrayLayer) {
    (void)mipLevel;
    (void)arrayLayer;
    return getNativeHandle();
}

void GLTexture::uploadData(const void* data, size_t dataSize,
                           uint32_t mipLevel, uint32_t arrayLayer,
                           uint32_t offsetX, uint32_t offsetY, uint32_t offsetZ,
                           uint32_t width, uint32_t height, uint32_t depth) {
    (void)dataSize;

    if (width == 0) width = std::max(1u, m_desc.width >> mipLevel);
    if (height == 0) height = std::max(1u, m_desc.height >> mipLevel);
    if (depth == 0) depth = std::max(1u, m_desc.depth >> mipLevel);

    GLenum format = GLDevice::toGLFormat(m_desc.format);
    GLenum type = GLDevice::toGLType(m_desc.format);

    glBindTexture(m_target, m_texture);

    switch (m_desc.type) {
        case TextureType::Texture1D:
            glTexSubImage1D(m_target, mipLevel, offsetX, width, format, type, data);
            break;

        case TextureType::Texture2D:
            glTexSubImage2D(m_target, mipLevel, offsetX, offsetY,
                            width, height, format, type, data);
            break;

        case TextureType::Texture3D:
            glTexSubImage3D(m_target, mipLevel, offsetX, offsetY, offsetZ,
                            width, height, depth, format, type, data);
            break;

        case TextureType::TextureCube:
            glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + arrayLayer, mipLevel,
                            offsetX, offsetY, width, height, format, type, data);
            break;

        case TextureType::Texture2DArray:
        case TextureType::TextureCubeArray:
            glTexSubImage3D(m_target, mipLevel, offsetX, offsetY, arrayLayer,
                            width, height, 1, format, type, data);
            break;
    }

    glBindTexture(m_target, 0);
}

void GLTexture::generateMipmaps() {
    glBindTexture(m_target, m_texture);
    glGenerateMipmap(m_target);
    glBindTexture(m_target, 0);
}

// ============================================================================
// GL SAMPLER
// ============================================================================

GLSampler::GLSampler(GLDevice* device, const SamplerDesc& desc)
    : m_desc(desc) {
    (void)device;

    glGenSamplers(1, &m_sampler);

    // Filtering
    GLenum minFilter = GLDevice::toGLFilter(desc.minFilter);
    if (desc.mipmapMode == MipmapMode::Linear) {
        minFilter = (desc.minFilter == Filter::Linear) ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    } else {
        minFilter = (desc.minFilter == Filter::Linear) ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
    }

    glSamplerParameteri(m_sampler, GL_TEXTURE_MIN_FILTER, minFilter);
    glSamplerParameteri(m_sampler, GL_TEXTURE_MAG_FILTER, GLDevice::toGLFilter(desc.magFilter));

    // Wrapping
    glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_S, GLDevice::toGLAddressMode(desc.addressU));
    glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_T, GLDevice::toGLAddressMode(desc.addressV));
    glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_R, GLDevice::toGLAddressMode(desc.addressW));

    // LOD
    glSamplerParameterf(m_sampler, GL_TEXTURE_LOD_BIAS, desc.mipLodBias);
    glSamplerParameterf(m_sampler, GL_TEXTURE_MIN_LOD, desc.minLod);
    glSamplerParameterf(m_sampler, GL_TEXTURE_MAX_LOD, desc.maxLod);

    // Anisotropy
    if (desc.anisotropyEnable) {
        glSamplerParameterf(m_sampler, GL_TEXTURE_MAX_ANISOTROPY, desc.maxAnisotropy);
    }

    // Comparison
    if (desc.compareEnable) {
        glSamplerParameteri(m_sampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(m_sampler, GL_TEXTURE_COMPARE_FUNC, GLDevice::toGLCompareOp(desc.compareOp));
    }

    // Border color
    glSamplerParameterfv(m_sampler, GL_TEXTURE_BORDER_COLOR, &desc.borderColor[0]);
}

GLSampler::~GLSampler() {
    if (m_sampler != 0) {
        glDeleteSamplers(1, &m_sampler);
    }
}

} // namespace RHI
