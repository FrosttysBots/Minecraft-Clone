#include "VKDescriptorSet.h"
#include "VKDevice.h"
#include <iostream>

namespace RHI {

// ============================================================================
// VK DESCRIPTOR SET
// ============================================================================

VKDescriptorSet::VKDescriptorSet(VKDevice* device, VKDescriptorSetLayout* layout, VkDescriptorSet set)
    : m_device(device), m_layout(layout), m_descriptorSet(set) {
}

void VKDescriptorSet::update(const std::vector<DescriptorWrite>& writes) {
    std::vector<VkWriteDescriptorSet> vkWrites;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;

    // Pre-allocate to avoid reallocation invalidating pointers
    bufferInfos.reserve(writes.size());
    imageInfos.reserve(writes.size());

    for (const auto& write : writes) {
        VkWriteDescriptorSet vkWrite{};
        vkWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vkWrite.dstSet = m_descriptorSet;
        vkWrite.dstBinding = write.binding;
        vkWrite.dstArrayElement = write.arrayElement;
        vkWrite.descriptorCount = 1;
        vkWrite.descriptorType = VKDevice::toVkDescriptorType(write.type);

        switch (write.type) {
            case DescriptorType::UniformBuffer:
            case DescriptorType::StorageBuffer:
            case DescriptorType::UniformBufferDynamic:
            case DescriptorType::StorageBufferDynamic: {
                VkDescriptorBufferInfo bufferInfo{};
                auto* vkBuffer = static_cast<VKBuffer*>(write.buffer);
                bufferInfo.buffer = vkBuffer->getVkBuffer();
                bufferInfo.offset = write.bufferOffset;
                bufferInfo.range = write.bufferRange == 0 ? VK_WHOLE_SIZE : write.bufferRange;
                bufferInfos.push_back(bufferInfo);
                vkWrite.pBufferInfo = &bufferInfos.back();
                break;
            }

            case DescriptorType::SampledTexture:
            case DescriptorType::StorageTexture: {
                VkDescriptorImageInfo imageInfo{};
                auto* vkTexture = static_cast<VKTexture*>(write.texture);
                imageInfo.imageView = vkTexture->getVkImageView();
                imageInfo.imageLayout = write.type == DescriptorType::StorageTexture ?
                    VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                if (write.sampler) {
                    auto* vkSampler = static_cast<VKSampler*>(write.sampler);
                    imageInfo.sampler = vkSampler->getVkSampler();
                }
                imageInfos.push_back(imageInfo);
                vkWrite.pImageInfo = &imageInfos.back();
                break;
            }

            case DescriptorType::Sampler: {
                VkDescriptorImageInfo imageInfo{};
                auto* vkSampler = static_cast<VKSampler*>(write.sampler);
                imageInfo.sampler = vkSampler->getVkSampler();
                imageInfos.push_back(imageInfo);
                vkWrite.pImageInfo = &imageInfos.back();
                break;
            }

            default:
                std::cerr << "[VKDescriptorSet] Unsupported descriptor type" << std::endl;
                continue;
        }

        vkWrites.push_back(vkWrite);
    }

    if (!vkWrites.empty()) {
        vkUpdateDescriptorSets(m_device->getDevice(), static_cast<uint32_t>(vkWrites.size()),
                               vkWrites.data(), 0, nullptr);
    }
}

void VKDescriptorSet::updateBuffer(uint32_t binding, RHIBuffer* buffer, size_t offset, size_t range) {
    auto* vkBuffer = static_cast<VKBuffer*>(buffer);

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = vkBuffer->getVkBuffer();
    bufferInfo.offset = offset;
    bufferInfo.range = range == 0 ? VK_WHOLE_SIZE : range;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;  // Assume uniform buffer
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_device->getDevice(), 1, &write, 0, nullptr);
}

void VKDescriptorSet::updateTexture(uint32_t binding, RHITexture* texture, RHISampler* sampler) {
    auto* vkTexture = static_cast<VKTexture*>(texture);
    auto* vkSampler = static_cast<VKSampler*>(sampler);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = vkTexture->getVkImageView();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.sampler = vkSampler ? vkSampler->getVkSampler() : VK_NULL_HANDLE;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = sampler ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_device->getDevice(), 1, &write, 0, nullptr);
}

// ============================================================================
// VK DESCRIPTOR POOL
// ============================================================================

VKDescriptorPool::VKDescriptorPool(VKDevice* device, const DescriptorPoolDesc& desc)
    : m_device(device), m_desc(desc) {

    std::vector<VkDescriptorPoolSize> poolSizes;

    // Convert pool sizes
    for (const auto& size : desc.poolSizes) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VKDevice::toVkDescriptorType(size.type);
        poolSize.descriptorCount = size.count;
        poolSizes.push_back(poolSize);
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = desc.allowFreeDescriptorSet ? VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT : 0;
    poolInfo.maxSets = desc.maxSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    VkResult result = vkCreateDescriptorPool(m_device->getDevice(), &poolInfo, nullptr, &m_pool);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKDescriptorPool] Failed to create descriptor pool" << std::endl;
    }
}

VKDescriptorPool::~VKDescriptorPool() {
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device->getDevice(), m_pool, nullptr);
    }
}

std::unique_ptr<RHIDescriptorSet> VKDescriptorPool::allocate(RHIDescriptorSetLayout* layout) {
    auto* vkLayout = static_cast<VKDescriptorSetLayout*>(layout);
    VkDescriptorSetLayout layouts[] = {vkLayout->getVkLayout()};

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = layouts;

    VkDescriptorSet descriptorSet;
    VkResult result = vkAllocateDescriptorSets(m_device->getDevice(), &allocInfo, &descriptorSet);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKDescriptorPool] Failed to allocate descriptor set" << std::endl;
        return nullptr;
    }

    return std::make_unique<VKDescriptorSet>(m_device, vkLayout, descriptorSet);
}

void VKDescriptorPool::reset() {
    vkResetDescriptorPool(m_device->getDevice(), m_pool, 0);
}

} // namespace RHI
