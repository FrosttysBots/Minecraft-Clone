#pragma once

#include "RHITypes.h"

namespace RHI {

// Forward declarations
class RHIDevice;

// ============================================================================
// RHI TEXTURE INTERFACE
// ============================================================================
// Abstract texture interface for 1D, 2D, 3D, cube, and array textures.
// Supports render targets, depth buffers, and storage images.

class RHITexture {
public:
    virtual ~RHITexture() = default;

    // Get texture descriptor
    virtual const TextureDesc& getDesc() const = 0;

    // Get native handle (GLuint for OpenGL, VkImage for Vulkan)
    virtual void* getNativeHandle() const = 0;

    // Get native view handle (same as handle for OpenGL, VkImageView for Vulkan)
    virtual void* getNativeViewHandle() const = 0;

    // ========================================================================
    // SUBRESOURCE VIEWS
    // ========================================================================

    // Create a view of a specific mip level
    // For Vulkan: Creates VkImageView
    // For OpenGL: Returns texture handle (views are implicit)
    virtual void* getMipView(uint32_t mipLevel) = 0;

    // Create a view of a specific array layer
    virtual void* getLayerView(uint32_t arrayLayer) = 0;

    // Create a view of a specific mip level and array layer
    virtual void* getSubresourceView(uint32_t mipLevel, uint32_t arrayLayer) = 0;

    // ========================================================================
    // DATA OPERATIONS
    // ========================================================================

    // Upload texture data
    // data layout: tightly packed rows, mip levels, then array layers
    virtual void uploadData(const void* data, size_t dataSize,
                            uint32_t mipLevel = 0,
                            uint32_t arrayLayer = 0,
                            uint32_t offsetX = 0,
                            uint32_t offsetY = 0,
                            uint32_t offsetZ = 0,
                            uint32_t width = 0,   // 0 = full width
                            uint32_t height = 0,  // 0 = full height
                            uint32_t depth = 0) = 0;  // 0 = full depth

    // Generate mipmaps (texture must have been created with enough mip levels)
    virtual void generateMipmaps() = 0;

protected:
    RHITexture() = default;
};

// ============================================================================
// RHI SAMPLER INTERFACE
// ============================================================================
// Sampler object for texture filtering and addressing

class RHISampler {
public:
    virtual ~RHISampler() = default;

    virtual const SamplerDesc& getDesc() const = 0;
    virtual void* getNativeHandle() const = 0;

protected:
    RHISampler() = default;
};

// ============================================================================
// TEXTURE VIEW
// ============================================================================
// A specific view of a texture for binding

struct TextureView {
    RHITexture* texture = nullptr;
    uint32_t baseMipLevel = 0;
    uint32_t mipLevelCount = 1;
    uint32_t baseArrayLayer = 0;
    uint32_t arrayLayerCount = 1;

    TextureView() = default;
    explicit TextureView(RHITexture* tex)
        : texture(tex)
        , baseMipLevel(0)
        , mipLevelCount(tex ? tex->getDesc().mipLevels : 1)
        , baseArrayLayer(0)
        , arrayLayerCount(tex ? tex->getDesc().arrayLayers : 1) {}
};

} // namespace RHI
