#pragma once

#include "../RHIBuffer.h"
#include <glad/gl.h>

namespace RHI {

class GLDevice;

// ============================================================================
// GL BUFFER
// ============================================================================

class GLBuffer : public RHIBuffer {
public:
    GLBuffer(GLDevice* device, const BufferDesc& desc);
    ~GLBuffer() override;

    // Non-copyable
    GLBuffer(const GLBuffer&) = delete;
    GLBuffer& operator=(const GLBuffer&) = delete;

    // RHIBuffer interface
    const BufferDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return reinterpret_cast<void*>(static_cast<uintptr_t>(m_buffer)); }

    void* map() override;
    void* mapRange(size_t offset, size_t size) override;
    void unmap() override;
    bool isMapped() const override { return m_mappedPtr != nullptr; }
    void* getPersistentPtr() const override { return m_persistentPtr; }

    void uploadData(const void* data, size_t size, size_t offset) override;
    void flush(size_t offset, size_t size) override;
    void invalidate(size_t offset, size_t size) override;

    // GL-specific
    GLuint getGLBuffer() const { return m_buffer; }
    GLenum getGLTarget() const { return m_target; }

private:
    GLDevice* m_device = nullptr;
    BufferDesc m_desc;
    GLuint m_buffer = 0;
    GLenum m_target = GL_ARRAY_BUFFER;
    void* m_persistentPtr = nullptr;
    void* m_mappedPtr = nullptr;
    bool m_isPersistent = false;
};

} // namespace RHI
