#include "VKBuffer.h"
#include "VKDevice.h"
#include <cstring>
#include <iostream>

namespace RHI {

VKBuffer::VKBuffer(VKDevice* device, const BufferDesc& desc)
    : m_device(device), m_desc(desc) {

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.usage = VKDevice::toVkBufferUsage(desc.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Enable buffer device address for advanced features
    bufferInfo.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo allocInfo{};

    // Determine memory properties based on usage
    bool persistentMap = desc.persistentMap || desc.memory == MemoryUsage::Persistent;

    switch (desc.memory) {
        case MemoryUsage::GpuOnly:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;

        case MemoryUsage::CpuToGpu:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            break;

        case MemoryUsage::GpuToCpu:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            break;

        case MemoryUsage::CpuOnly:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            break;

        case MemoryUsage::Persistent:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                              VMA_ALLOCATION_CREATE_MAPPED_BIT;
            persistentMap = true;
            break;
    }

    if (persistentMap) {
        allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VkResult result = vmaCreateBuffer(m_device->getAllocator(), &bufferInfo, &allocInfo,
                                       &m_buffer, &m_allocation, &m_allocationInfo);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKBuffer] Failed to create buffer" << std::endl;
        return;
    }

    if (persistentMap && m_allocationInfo.pMappedData) {
        m_persistentPtr = m_allocationInfo.pMappedData;
    }

    // Set debug name
    if (!desc.debugName.empty()) {
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_buffer);
        nameInfo.pObjectName = desc.debugName.c_str();
        vkSetDebugUtilsObjectNameEXT(m_device->getDevice(), &nameInfo);
    }
}

VKBuffer::~VKBuffer() {
    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_device->getAllocator(), m_buffer, m_allocation);
    }
}

void* VKBuffer::map() {
    if (m_persistentPtr) {
        return m_persistentPtr;
    }

    if (m_mappedPtr) {
        return m_mappedPtr;
    }

    if (vmaMapMemory(m_device->getAllocator(), m_allocation, &m_mappedPtr) != VK_SUCCESS) {
        return nullptr;
    }

    return m_mappedPtr;
}

void* VKBuffer::mapRange(size_t offset, size_t size) {
    (void)size;

    void* ptr = map();
    if (ptr) {
        return static_cast<uint8_t*>(ptr) + offset;
    }
    return nullptr;
}

void VKBuffer::unmap() {
    if (m_persistentPtr) {
        return;  // Persistent buffers stay mapped
    }

    if (m_mappedPtr) {
        vmaUnmapMemory(m_device->getAllocator(), m_allocation);
        m_mappedPtr = nullptr;
    }
}

void VKBuffer::uploadData(const void* data, size_t size, size_t offset) {
    void* ptr = map();
    if (ptr) {
        std::memcpy(static_cast<uint8_t*>(ptr) + offset, data, size);
        flush(offset, size);
        if (!m_persistentPtr) {
            unmap();
        }
    }
}

void VKBuffer::flush(size_t offset, size_t size) {
    if (size == 0) size = m_desc.size - offset;
    vmaFlushAllocation(m_device->getAllocator(), m_allocation, offset, size);
}

void VKBuffer::invalidate(size_t offset, size_t size) {
    if (size == 0) size = m_desc.size - offset;
    vmaInvalidateAllocation(m_device->getAllocator(), m_allocation, offset, size);
}

} // namespace RHI
