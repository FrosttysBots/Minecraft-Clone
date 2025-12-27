#pragma once

#include "../RHIBuffer.h"
#include <volk.h>
#include <vk_mem_alloc.h>

namespace RHI {

class VKDevice;

// ============================================================================
// VK BUFFER
// ============================================================================

class VKBuffer : public RHIBuffer {
public:
    VKBuffer(VKDevice* device, const BufferDesc& desc);
    ~VKBuffer() override;

    // Non-copyable
    VKBuffer(const VKBuffer&) = delete;
    VKBuffer& operator=(const VKBuffer&) = delete;

    // RHIBuffer interface
    const BufferDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return m_buffer; }

    void* map() override;
    void* mapRange(size_t offset, size_t size) override;
    void unmap() override;
    bool isMapped() const override { return m_mappedPtr != nullptr; }
    void* getPersistentPtr() const override { return m_persistentPtr; }

    void uploadData(const void* data, size_t size, size_t offset) override;
    void flush(size_t offset, size_t size) override;
    void invalidate(size_t offset, size_t size) override;

    // Vulkan-specific
    VkBuffer getVkBuffer() const { return m_buffer; }
    VmaAllocation getAllocation() const { return m_allocation; }

private:
    VKDevice* m_device = nullptr;
    BufferDesc m_desc;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo m_allocationInfo{};
    void* m_persistentPtr = nullptr;
    void* m_mappedPtr = nullptr;
};

} // namespace RHI
