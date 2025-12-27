#pragma once

#include "../RHIDescriptorSet.h"
#include "GLBuffer.h"
#include "GLTexture.h"
#include "GLPipeline.h"  // For GLDescriptorSetLayout
#include <glad/gl.h>
#include <variant>

namespace RHI {

class GLDevice;

// ============================================================================
// GL DESCRIPTOR SET
// ============================================================================
// In OpenGL, descriptor sets are just a collection of bindings that we
// apply when the set is bound

struct GLDescriptorBinding {
    uint32_t binding = 0;
    DescriptorType type = DescriptorType::UniformBuffer;

    // Buffer data
    GLBuffer* buffer = nullptr;
    size_t bufferOffset = 0;
    size_t bufferRange = 0;

    // Texture data
    GLTexture* texture = nullptr;
    GLSampler* sampler = nullptr;
};

class GLDescriptorSet : public RHIDescriptorSet {
public:
    GLDescriptorSet(GLDevice* device, GLDescriptorSetLayout* layout);

    RHIDescriptorSetLayout* getLayout() const override { return m_layout; }
    void* getNativeHandle() const override { return nullptr; }

    void update(const std::vector<DescriptorWrite>& writes) override;
    void updateBuffer(uint32_t binding, RHIBuffer* buffer, size_t offset, size_t range) override;
    void updateTexture(uint32_t binding, RHITexture* texture, RHISampler* sampler) override;

    // Apply bindings to OpenGL state
    void bind(uint32_t setIndex);

    const std::vector<GLDescriptorBinding>& getBindings() const { return m_bindings; }

private:
    GLDevice* m_device = nullptr;
    GLDescriptorSetLayout* m_layout = nullptr;
    std::vector<GLDescriptorBinding> m_bindings;
};

// ============================================================================
// GL DESCRIPTOR POOL
// ============================================================================
// In OpenGL, we don't really need pools - descriptor sets are lightweight

class GLDescriptorPool : public RHIDescriptorPool {
public:
    GLDescriptorPool(GLDevice* device, const DescriptorPoolDesc& desc);

    void* getNativeHandle() const override { return nullptr; }

    std::unique_ptr<RHIDescriptorSet> allocate(RHIDescriptorSetLayout* layout) override;
    void reset() override;

private:
    GLDevice* m_device = nullptr;
    DescriptorPoolDesc m_desc;
};

} // namespace RHI
