#pragma once

#include "RHITypes.h"
#include "RHIBuffer.h"
#include "RHITexture.h"

namespace RHI {

// ============================================================================
// DESCRIPTOR SET LAYOUT
// ============================================================================
// Defines the layout of bindings in a descriptor set
// For OpenGL: Used to track bindings, not a real object
// For Vulkan: VkDescriptorSetLayout

class RHIDescriptorSetLayout {
public:
    virtual ~RHIDescriptorSetLayout() = default;

    virtual const DescriptorSetLayoutDesc& getDesc() const = 0;
    virtual void* getNativeHandle() const = 0;

protected:
    RHIDescriptorSetLayout() = default;
};

// ============================================================================
// DESCRIPTOR SET
// ============================================================================
// A set of resource bindings for shaders
// For OpenGL: Tracks bindings for glBindBufferBase/glBindTextureUnit
// For Vulkan: VkDescriptorSet

struct DescriptorWrite {
    uint32_t binding = 0;
    uint32_t arrayElement = 0;
    DescriptorType type = DescriptorType::UniformBuffer;

    // Buffer binding (for uniform/storage buffers)
    RHIBuffer* buffer = nullptr;
    size_t bufferOffset = 0;
    size_t bufferRange = 0;  // 0 = whole buffer

    // Texture binding (for samplers, sampled/storage textures)
    RHITexture* texture = nullptr;
    RHISampler* sampler = nullptr;
};

class RHIDescriptorSet {
public:
    virtual ~RHIDescriptorSet() = default;

    virtual RHIDescriptorSetLayout* getLayout() const = 0;
    virtual void* getNativeHandle() const = 0;

    // Update descriptor bindings
    virtual void update(const std::vector<DescriptorWrite>& writes) = 0;

    // Update a single binding
    virtual void updateBuffer(uint32_t binding, RHIBuffer* buffer,
                              size_t offset = 0, size_t range = 0) = 0;
    virtual void updateTexture(uint32_t binding, RHITexture* texture,
                               RHISampler* sampler = nullptr) = 0;

protected:
    RHIDescriptorSet() = default;
};

// ============================================================================
// DESCRIPTOR POOL
// ============================================================================
// Pool for allocating descriptor sets
// For OpenGL: Not needed (descriptor sets are lightweight)
// For Vulkan: VkDescriptorPool

struct DescriptorPoolSize {
    DescriptorType type = DescriptorType::UniformBuffer;
    uint32_t count = 1;
};

struct DescriptorPoolDesc {
    std::vector<DescriptorPoolSize> poolSizes;
    uint32_t maxSets = 100;
    bool allowFreeDescriptorSet = false;  // VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
};

class RHIDescriptorPool {
public:
    virtual ~RHIDescriptorPool() = default;

    virtual void* getNativeHandle() const = 0;

    // Allocate a descriptor set from this pool
    virtual std::unique_ptr<RHIDescriptorSet> allocate(RHIDescriptorSetLayout* layout) = 0;

    // Reset all allocations in the pool
    virtual void reset() = 0;

protected:
    RHIDescriptorPool() = default;
};

} // namespace RHI
