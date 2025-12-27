#pragma once

#include "RHITypes.h"

namespace RHI {

// Forward declarations
class RHIDevice;

// ============================================================================
// RHI BUFFER INTERFACE
// ============================================================================
// Abstract buffer interface for vertex, index, uniform, and storage buffers.
// Supports both GPU-only and persistently mapped buffers (like VertexPool).
//
// Usage patterns:
// 1. GPU-only buffer: Create, upload data via staging, use in shaders
// 2. Staging buffer:  Create with CpuToGpu, map, write, unmap, use as copy source
// 3. Persistent:      Create with persistentMap=true, keep mapped, write anytime
//
class RHIBuffer {
public:
    virtual ~RHIBuffer() = default;

    // Get buffer descriptor
    virtual const BufferDesc& getDesc() const = 0;

    // Get native handle (GLuint for OpenGL, VkBuffer for Vulkan)
    virtual void* getNativeHandle() const = 0;

    // ========================================================================
    // MAPPING OPERATIONS
    // ========================================================================

    // Map buffer for CPU access
    // For persistent buffers, returns the persistent pointer
    // For non-persistent, maps temporarily (must call unmap)
    // Returns nullptr on failure
    virtual void* map() = 0;

    // Map a range of the buffer
    // offset and size in bytes
    virtual void* mapRange(size_t offset, size_t size) = 0;

    // Unmap buffer (no-op for persistent buffers)
    virtual void unmap() = 0;

    // Check if buffer is currently mapped
    virtual bool isMapped() const = 0;

    // Get persistent mapped pointer (only for persistentMap=true buffers)
    // Returns nullptr if not persistently mapped
    virtual void* getPersistentPtr() const = 0;

    // ========================================================================
    // DATA OPERATIONS
    // ========================================================================

    // Upload data to buffer (for non-persistent buffers)
    // Internally may use staging buffer or direct mapping
    virtual void uploadData(const void* data, size_t size, size_t offset = 0) = 0;

    // Flush mapped memory range (for non-coherent memory)
    // Call after writing to mapped memory to ensure GPU sees the writes
    virtual void flush(size_t offset = 0, size_t size = 0) = 0;

    // Invalidate mapped memory range (for readback)
    // Call before reading from mapped memory to ensure CPU sees GPU writes
    virtual void invalidate(size_t offset = 0, size_t size = 0) = 0;

protected:
    RHIBuffer() = default;
};

// ============================================================================
// BUFFER VIEW
// ============================================================================
// A view into a portion of a buffer for binding to shaders

struct BufferView {
    RHIBuffer* buffer = nullptr;
    size_t offset = 0;
    size_t size = 0;  // 0 means entire remaining buffer

    BufferView() = default;
    BufferView(RHIBuffer* buf) : buffer(buf), offset(0), size(buf ? buf->getDesc().size : 0) {}
    BufferView(RHIBuffer* buf, size_t off, size_t sz) : buffer(buf), offset(off), size(sz) {}
};

} // namespace RHI
