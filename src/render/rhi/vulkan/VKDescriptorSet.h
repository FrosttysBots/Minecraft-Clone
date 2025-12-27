#pragma once

#include "../RHIDescriptorSet.h"
#include "VKBuffer.h"
#include "VKTexture.h"
#include "VKPipeline.h"
#include <volk.h>
#include <vector>

namespace RHI {

class VKDevice;

// ============================================================================
// VK DESCRIPTOR SET
// ============================================================================

class VKDescriptorSet : public RHIDescriptorSet {
public:
    VKDescriptorSet(VKDevice* device, VKDescriptorSetLayout* layout, VkDescriptorSet set);
    ~VKDescriptorSet() override = default;  // Set is owned by pool

    RHIDescriptorSetLayout* getLayout() const override { return m_layout; }
    void* getNativeHandle() const override { return m_descriptorSet; }

    void update(const std::vector<DescriptorWrite>& writes) override;
    void updateBuffer(uint32_t binding, RHIBuffer* buffer, size_t offset, size_t range) override;
    void updateTexture(uint32_t binding, RHITexture* texture, RHISampler* sampler) override;

    VkDescriptorSet getVkDescriptorSet() const { return m_descriptorSet; }

private:
    VKDevice* m_device = nullptr;
    VKDescriptorSetLayout* m_layout = nullptr;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
};

// ============================================================================
// VK DESCRIPTOR POOL
// ============================================================================

class VKDescriptorPool : public RHIDescriptorPool {
public:
    VKDescriptorPool(VKDevice* device, const DescriptorPoolDesc& desc);
    ~VKDescriptorPool() override;

    void* getNativeHandle() const override { return m_pool; }

    std::unique_ptr<RHIDescriptorSet> allocate(RHIDescriptorSetLayout* layout) override;
    void reset() override;

    VkDescriptorPool getVkPool() const { return m_pool; }

private:
    VKDevice* m_device = nullptr;
    DescriptorPoolDesc m_desc;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
};

} // namespace RHI
