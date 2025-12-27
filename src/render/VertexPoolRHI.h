#pragma once

// ============================================================================
// RHI-BASED VERTEX POOL SYSTEM
// ============================================================================
// RHI abstraction of the VertexPool for OpenGL/Vulkan compatibility.
// Uses RHI buffer with persistent mapping for efficient CPU->GPU transfers.
//
// For OpenGL: Uses GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT
// For Vulkan: Uses VMA with VMA_ALLOCATION_CREATE_MAPPED_BIT
// ============================================================================

#include "ChunkMesh.h"
#include "rhi/RHI.h"
#include <vector>
#include <mutex>
#include <cstring>
#include <iostream>

namespace Render {

// Configuration (same as original VertexPool)
constexpr size_t RHI_VERTEX_POOL_SIZE_MB = 512;
constexpr size_t RHI_VERTEX_POOL_BUCKET_SIZE = 64 * 1024;  // 64KB per bucket
constexpr size_t RHI_VERTEX_POOL_SIZE = RHI_VERTEX_POOL_SIZE_MB * 1024 * 1024;
constexpr size_t RHI_VERTEX_POOL_BUCKET_COUNT = RHI_VERTEX_POOL_SIZE / RHI_VERTEX_POOL_BUCKET_SIZE;
constexpr size_t RHI_MAX_VERTICES_PER_BUCKET = RHI_VERTEX_POOL_BUCKET_SIZE / sizeof(PackedChunkVertex);

// Bucket handle - identifies a region in the pool
struct RHIPoolBucket {
    uint32_t index = UINT32_MAX;
    uint32_t vertexCount = 0;

    bool isValid() const { return index != UINT32_MAX; }
    void invalidate() { index = UINT32_MAX; vertexCount = 0; }

    size_t getByteOffset() const { return static_cast<size_t>(index) * RHI_VERTEX_POOL_BUCKET_SIZE; }
    uint32_t getVertexOffset() const {
        return static_cast<uint32_t>(getByteOffset() / sizeof(PackedChunkVertex));
    }
};

// RHI Vertex Pool Manager
class VertexPoolRHI {
public:
    VertexPoolRHI() = default;
    ~VertexPoolRHI() { shutdown(); }

    // Non-copyable
    VertexPoolRHI(const VertexPoolRHI&) = delete;
    VertexPoolRHI& operator=(const VertexPoolRHI&) = delete;

    // Initialize with RHI device
    bool initialize(RHI::RHIDevice* device) {
        if (m_initialized || !device) return m_initialized;

        m_device = device;

        // Create large vertex buffer with persistent mapping
        RHI::BufferDesc bufferDesc{};
        bufferDesc.size = RHI_VERTEX_POOL_SIZE;
        bufferDesc.usage = RHI::BufferUsage::Vertex;
        bufferDesc.memory = RHI::MemoryUsage::Persistent;  // Persistent mapped like VertexPool
        bufferDesc.persistentMap = true;
        bufferDesc.debugName = "VertexPoolRHI_MainBuffer";

        m_buffer = device->createBuffer(bufferDesc);
        if (!m_buffer) {
            std::cerr << "[VertexPoolRHI] Failed to create vertex buffer" << std::endl;
            return false;
        }

        // Map the buffer persistently
        m_mappedPtr = static_cast<uint8_t*>(m_buffer->map());
        if (!m_mappedPtr) {
            std::cerr << "[VertexPoolRHI] Failed to map vertex buffer" << std::endl;
            m_buffer.reset();
            return false;
        }

        // Initialize free bucket list
        m_freeBuckets.reserve(RHI_VERTEX_POOL_BUCKET_COUNT);
        for (size_t i = 0; i < RHI_VERTEX_POOL_BUCKET_COUNT; i++) {
            m_freeBuckets.push_back(static_cast<uint32_t>(i));
        }

        m_initialized = true;
        std::cout << "[VertexPoolRHI] Initialized " << RHI_VERTEX_POOL_SIZE_MB << "MB ("
                  << RHI_VERTEX_POOL_BUCKET_COUNT << " buckets of "
                  << RHI_VERTEX_POOL_BUCKET_SIZE / 1024 << "KB each)" << std::endl;

        return true;
    }

    void shutdown() {
        if (!m_initialized) return;

        if (m_buffer && m_mappedPtr) {
            m_buffer->unmap();
            m_mappedPtr = nullptr;
        }

        m_buffer.reset();
        m_freeBuckets.clear();
        m_initialized = false;
        m_device = nullptr;
    }

    // Allocate a bucket and upload vertex data
    RHIPoolBucket allocateAndUpload(const std::vector<PackedChunkVertex>& vertices) {
        RHIPoolBucket bucket;

        if (vertices.empty() || !m_initialized) return bucket;

        size_t dataSize = vertices.size() * sizeof(PackedChunkVertex);
        if (dataSize > RHI_VERTEX_POOL_BUCKET_SIZE) {
            // Data too large for single bucket
            return bucket;
        }

        // Get a free bucket
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_freeBuckets.empty()) {
                return bucket;  // Pool exhausted
            }
            bucket.index = m_freeBuckets.back();
            m_freeBuckets.pop_back();
        }

        bucket.vertexCount = static_cast<uint32_t>(vertices.size());

        // Copy data to mapped buffer
        size_t offset = bucket.getByteOffset();
        std::memcpy(m_mappedPtr + offset, vertices.data(), dataSize);

        return bucket;
    }

    // Release a bucket back to the pool
    void release(RHIPoolBucket& bucket) {
        if (!bucket.isValid()) return;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_freeBuckets.push_back(bucket.index);
        }

        bucket.invalidate();
    }

    // Bind buffer for rendering (for hybrid OpenGL path)
    void bind(RHI::RHICommandBuffer* cmd) const {
        if (!m_buffer) return;
        cmd->bindVertexBuffer(0, m_buffer.get(), 0);  // binding=0, buffer, offset=0
    }

    // Draw vertices from a bucket using RHI command buffer
    void draw(RHI::RHICommandBuffer* cmd, const RHIPoolBucket& bucket) const {
        if (!bucket.isValid() || bucket.vertexCount == 0) return;
        cmd->draw(bucket.vertexCount, 1, bucket.getVertexOffset(), 0);
    }

    // Get underlying RHI buffer for direct access
    RHI::RHIBuffer* getBuffer() const { return m_buffer.get(); }

    // Get native handle for OpenGL interop
    void* getNativeBufferHandle() const {
        return m_buffer ? m_buffer->getNativeHandle() : nullptr;
    }

    // Get GL buffer ID for hybrid OpenGL path
    uint32_t getGLBufferID() const {
        void* handle = getNativeBufferHandle();
        return handle ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(handle)) : 0;
    }

    // Get mapped pointer for direct CPU access (hybrid path)
    uint8_t* getMappedPointer() const { return m_mappedPtr; }

    // Statistics
    size_t getFreeBucketCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_freeBuckets.size();
    }

    size_t getTotalBucketCount() const { return RHI_VERTEX_POOL_BUCKET_COUNT; }
    size_t getUsedBucketCount() const { return RHI_VERTEX_POOL_BUCKET_COUNT - getFreeBucketCount(); }

    float getUtilization() const {
        return 1.0f - static_cast<float>(getFreeBucketCount()) / RHI_VERTEX_POOL_BUCKET_COUNT;
    }

    bool isInitialized() const { return m_initialized; }

private:
    RHI::RHIDevice* m_device = nullptr;
    std::unique_ptr<RHI::RHIBuffer> m_buffer;
    uint8_t* m_mappedPtr = nullptr;
    bool m_initialized = false;

    std::vector<uint32_t> m_freeBuckets;
    mutable std::mutex m_mutex;
};

} // namespace Render
